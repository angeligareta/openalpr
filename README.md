# OPENALPR

## Introduction
This fork of [OPENALPR](https://github.com/openalpr/openalpr) is a modification for detecting a parking plate when a button is clicked. In order to do that, we use a socket and listen to its content until a message with the content 'Z' is received.

## How to use
Once the camera is installed, we would need to get the IP address that shows the streaming, the format would have tje following appearance: «http://192.168.0.90/mjpg/video.mjpg».

First of all, we would need to install Docker and create the container.
```
docker build -t atecresa_parking https://github.com/angeligareta/openalpr.git
```
After that, we need to download the folders config and runtime_data from this repository. Lastly we execute the docker in the folder where we downloaded the configuration files.
```
docker run -p 8080:8080 -it –rm -v $(pwd):/data:ro atecresa_parking -a ‘http://192.168.0.90/mjpg/video.mjpg’ -p 8080
```

Finally the program will start and listen to the port 8080, waiting for a message 'Z' to take a picture and send the detected label.
```
PORT 8080 added!
Client connected
Setting new Camera address: http://192.168.0.90/mjpg/video.mjpg
Video stream connecting…
Video stream connected
Message received: Z

NET MODE: TAKING PICTURE…
Total Time to process image: 60.6686ms.
Message sent: 1473JPY
```
