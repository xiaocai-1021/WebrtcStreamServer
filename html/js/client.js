class EventDispatcher {
  constructor() {
    this.eventListeners = {};
  }

  addEventListener(eventType, listener) {
    if (this.eventListeners[eventType] === undefined) {
      this.eventListeners[eventType] = [];
    }
    this.eventListeners[eventType].push(listener);
  };

  removeEventListener(eventType, listener) {
    if (!this.eventListeners[eventType]) {
      return;
    }
    const index = this.eventListeners[eventType].indexOf(listener);
    if (index !== -1) {
      this.eventListeners[eventType].splice(index, 1);
    }
  };

  clearEventListener(eventType) {
    this.eventListeners[eventType] = [];
  };

  dispatchEvent(event) {
    if (!this.eventListeners[event.type]) {
      return;
    }
    this.eventListeners[event.type].map(function (listener) {
      listener(event);
    });
  };
};

class WebrtcStreamClient extends EventDispatcher {
  constructor(signallingServerIp, signallingServerPort) {
    super();
    this.pc = null;
    this.signallingServerIp = signallingServerIp;
    this.signallingServerPort = signallingServerPort;
  }

  /**
   * 
   * @param {*} streamId 
   */
  async connect(streamId) {
    this.pc = new RTCPeerConnection(null);
    this.pc.addTransceiver("video", { direction: "recvonly" });
    this.pc.addTransceiver("audio", { direction: "recvonly" });
    this.pc.onaddstream = this._onaddstream.bind(this);
    const offer = await this.pc.createOffer();
    await this.pc.setLocalDescription(offer);

    var addr = "http://" + this.signallingServerIp + ":" + this.signallingServerPort + "/play";
    let res = await this._makeAjaxCall('POST', { streamId: streamId, offer: offer.sdp }, addr);

    if (!res.error)
      this._gotAnswer(res.answer);
    else
      console.log("Connect failed.");
  }

  close() {
    if (this.pc) {
      this.pc.close();
      this.pc = null;
    }
  }

  _onaddstream(stream) {
    super.dispatchEvent({ type: "onaddstream", data: stream });
  }

  async _gotAnswer(answer) {
    var desc = new RTCSessionDescription();
    desc.sdp = answer;
    desc.type = 'answer';
    await this.pc.setRemoteDescription(desc);
  }

  _makeAjaxCall(methodType, body, url) {
    let promiseObj = new Promise((resolve, reject) => {
      let xhr = new XMLHttpRequest();
      xhr.onreadystatechange = function () {
        if (xhr.readyState === 4) {
          if (xhr.status === 200) {
            let resp;
            try {
              resp = JSON.parse(xhr.responseText);
            } catch (e) {
              resp = xhr.responseText;
            }
            resolve(resp);
          } else {
            reject(xhr.status);
          }
        }
      }

      xhr.open(methodType, url, true);
      if (body !== undefined) {
        console.log(JSON.stringify(body));
        xhr.send(JSON.stringify(body));
      } else {
        xhr.send();
      }
    });
    return promiseObj;
  }
};