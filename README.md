# Webrtc stream server
[![CMake](https://github.com/wxxit/WebrtcStreamServer/actions/workflows/cmake.yml/badge.svg)](https://github.com/wxxit/WebrtcStreamServer/actions/workflows/cmake.yml)    
WebrtcStreamServer accepts RTMP stream and plays it with webrtc.

## Platform
Ubuntu 18.04

## Install dependencies
./install.sh

## Build
mkdir build  
cd build  
cmake ..  
make

## Run
+ Fill in the configuration file named config.toml correctly.
+ ./WebrtcStreamServer

## How to play stream.
There is an example under the HTML folder.

## Stream manager REST API
***

### Add Stream
**POST ${host}/streams**

Description:<br>
This request can add a stream.<br>

request body:

| type | content |
|:-------------|:-------|
|      json     |  object(streamConfig) |

    object(streamConfig): // Configuration used to add a stream.
    {
        url: string(url), // Url of the stream, required
    }
response body:

| type | content |
|:-------------|:-------|
|      json     |  object(AddStreamResponse) |
    object(AddStreamResponse):
    {
      error: bool(error); // Is this request successful.
      id: string(id); // ID of the stream.
    }

***
### Remove Stream
**DELETE ${host}/streams/${streamId}**

Description:<br>
This request can remove a stream.<br>

request body:

  **Empty**

response body:

  **Empty**

***
### List Streams
**GET ${host}/streams**

Description:<br>
List the streams in server.<br>

request body:

  **Empty**

response body:

| type | content |
|:-------------|:-------|
|      json     |  Array of streamItem |
    object(streamItem):
    {
      id: string(id); //  ID of the stream.
      url: string(url); // Url of the stream.
    }
***
