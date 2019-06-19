/*
 * Copyright (c) 2015 OpenALPR Technology, Inc.
 * Open source Automated License Plate Recognition [http://www.openalpr.com]
 *
 * This file is part of OpenALPR. It has been modified by ATECRESA to allow socket communication.
*/

#include <cstdio>
#include <sstream>
#include <iostream>
#include <iterator>
#include <algorithm>

#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"

#include "tclap/CmdLine.h"
#include "support/filesystem.h"
#include "support/timing.h"
#include "support/platform.h"
#include "video/videobuffer.h"
#include "motiondetector.h"
#include "alpr.h"

// SOCKET LIBRARIES
#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
using namespace alpr;

const std::string MAIN_WINDOW_NAME = "ALPR main window";

int server_fd, new_socket, valread;
struct sockaddr_in address;
int opt = 1;
int addrlen = sizeof(address);

int PORT = 1234, CAMERA = 1;
const std::string SERVER_IP = "127.0.0.1";
const std::string TAKE_PICTURE_SIGNAL = "Z";
const std::string DEFAULT_LICENSE_PLATE = "NO_LEGIBLE";
const int USB_MODE = 0;
const int NET_MODE = 1;

/** Function Headers */
std::string detectandshow(Alpr *alpr, cv::Mat frame, std::string region, bool writeJson);
void sendLicensePlate(std::string license_plate);
int openSocket();
std::string listenToSocket();
std::string templatePattern = "es";

// This boolean is set to false when the user hits terminates (e.g., CTRL+C )
// so we can end infinite loops for things like video processing.
bool program_active = true;

int main(int argc, const char **argv)
{
  std::string configFile = "";
  std::string country;
  std::string camera_adress;
  bool outputJson = false;
  bool debug_mode = false;

  // TCLAP is a CmdLine utility to help parsing the arguments that the program receives.
  TCLAP::CmdLine cmd("OpenAlpr Command Line Utility", ' ', Alpr::getVersion());

  // Available commands
  TCLAP::ValueArg<std::string> countryCodeArg("c", "country", "Country code to identify (either us for USA or eu for Europe).  Default=eu", false, "eu", "country_code");
  TCLAP::ValueArg<std::string> configFileArg("", "config", "Path to the openalpr.conf file", false, "runtime_data/eu.conf", "config_file");
  TCLAP::ValueArg<int> portArg("p", "port", "Port to open server.  Default = 1234", false, 1234, "port");
  TCLAP::ValueArg<std::string> cameraAddressArg("a", "address", "Address to the ip camera", false, "", "address");
  TCLAP::ValueArg<int> cameraArg("", "camera", "Default video camera by usb = 0", false, 0, "camera");

  TCLAP::SwitchArg jsonSwitch("j", "json", "Output recognition results in JSON format.  Default=off", cmd, false);
  TCLAP::SwitchArg debugSwitch("", "debug", "Enable debug output.  Default=off", cmd, false);

  std::string runtimeDir = "runtime_data";
  try
  {
    // We must add the value args to the cmd, in order to allow it to parse them.
    cmd.add(configFileArg);
    cmd.add(countryCodeArg);
    cmd.add(portArg);
    cmd.add(cameraAddressArg);
    cmd.add(cameraArg);

    // We must parse all the arguments or we will get the default values!
    cmd.parse(argc, argv);

    configFile = configFileArg.getValue();
    country = countryCodeArg.getValue();
    debug_mode = debugSwitch.getValue();
    outputJson = jsonSwitch.getValue();

    PORT = portArg.getValue();
    CAMERA = cameraArg.getValue();
    camera_adress = cameraAddressArg.getValue();
  }
  catch (TCLAP::ArgException &e) // catch any exceptions
  {
    std::cerr << "error: " << e.error() << " for arg " << e.argId() << std::endl;
    return 1;
  }

  Alpr alpr(country, configFile, runtimeDir);
  alpr.setTopN(10);
  //alpr.setDefaultRegion("es");

  if (alpr.isLoaded() == false)
  {
    std::cerr << "Error loading OpenALPR" << std::endl;
    return 1;
  }

  // OPEN SOCKET
  std::cout << "PORT " << PORT << " added!\n";
  if (openSocket() < 0)
  {
    std::cerr << "Error opening socket. Please try again with a valid port and server listening.\n";
    return -1;
  }

  int camera_mode = (camera_adress != "") ? NET_MODE : USB_MODE;

  // Video camera to use
  cv::VideoCapture cap(CAMERA); // For usb mode

  VideoBuffer videoBuffer;
  if (camera_mode == NET_MODE)
  { // IP Camera Enabled
    videoBuffer.connect(camera_adress, 5);
    std::cout << "Setting new Camera address: " << camera_adress << "\n";
  }

  cv::Mat frame;
  // Infinite loop, always listening to the socket default signal.
  while (true)
  {
    // Automatic Input by socket. Loop until the signal is received
    std::string inputCommand = "";
    do
    {
      inputCommand = listenToSocket();
    } while (inputCommand[0] != TAKE_PICTURE_SIGNAL[0]);

    bool took_picture = false;
    std::vector<cv::Rect> regionsOfInterest;
    switch (camera_mode)
    {
    case NET_MODE:
      std::cout << "NET MODE: ";
      took_picture = videoBuffer.getLatestFrame(&frame, regionsOfInterest) != -1;
      break;

    default:
      std::cout << "USB MODE: ";
      took_picture = cap.read(frame);
      break;
    }

    if (!took_picture)
    {
      std::cout << "ERROR TAKING PICTURE...\n";
      continue;
    }
    else
    {
      std::cout << "TAKING PICTURE...\n";
    }

  // Write last-image for debugging
  cv:
    imwrite("last-image.jpg", frame);

    std::string bestLicensePlate = detectandshow(&alpr, frame, "", outputJson) + "\n";
    sendLicensePlate(bestLicensePlate);
  }

  videoBuffer.disconnect();
}

// Method that get two socket file descriptors, the server one and the client one.
int openSocket()
{
  // Creating socket file descriptor
  if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
  {
    perror("socket failed");
    exit(EXIT_FAILURE);
  }

  // Forcefully attaching socket to the port 8080
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,
                 &opt, sizeof(opt)))
  {
    perror("setsockopt");
    exit(EXIT_FAILURE);
  }

  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(PORT);

  // Forcefully attaching socket to the port 8080
  if (bind(server_fd, (struct sockaddr *)&address,
           sizeof(address)) < 0)
  {
    perror("bind failed");
    exit(EXIT_FAILURE);
  }
  if (listen(server_fd, 3) < 0)
  {
    perror("listen");
    exit(EXIT_FAILURE);
  }
  if ((new_socket = accept(server_fd, (struct sockaddr *)&address,
                           (socklen_t *)&addrlen)) < 0)
  {
    perror("accept");
    exit(EXIT_FAILURE);
  }

  std::cout << "Client connected\n";
  return 0;
}

// Method that listen to a socket and get the message!
std::string listenToSocket()
{
  char buffer[1024] = {0};
  valread = read(new_socket, buffer, 1024);
  if (valread == 0)
  {
    if (openSocket() < 0)
    {
      std::cerr << "Error opening socket. Please try again with a valid port and server listening.\n";
    }
  }
  std::cout << "Message received: " << buffer << "\n";
  return std::string(buffer);
}

// Method that sends the license plate obtained by socket
void sendLicensePlate(std::string license_plate)
{
  send(new_socket, license_plate.c_str(), license_plate.length(), 0);
  std::cout << "Message sent: " << license_plate << "\n";
}

// Method that uses the OPENALPR code to detect the licenseplate of the photo we captured.
std::string detectandshow(Alpr *alpr, cv::Mat frame, std::string region, bool writeJson)
{
  timespec startTime;
  getTimeMonotonic(&startTime);

  std::vector<AlprRegionOfInterest> regionsOfInterest;
  regionsOfInterest.push_back(AlprRegionOfInterest(0, 0, frame.cols, frame.rows));

  AlprResults results;
  bool gotResults = false;
  if (regionsOfInterest.size() > 0)
  {
    results = alpr->recognize(frame.data, frame.elemSize(),
                              frame.cols, frame.rows, regionsOfInterest);
    gotResults = true;
  }

  timespec endTime;
  getTimeMonotonic(&endTime);
  double totalProcessingTime = diffclock(startTime, endTime);
  std::cout << "Total Time to process image: " << totalProcessingTime << "ms." << std::endl;

  std::string bestLicensePlate = DEFAULT_LICENSE_PLATE;
  if (writeJson && gotResults)
  {
    std::cout << alpr->toJson(results) << std::endl;
  }
  else if (gotResults)
  {
    for (int i = 0; i < results.plates.size(); i++)
    {
      std::cout << "plate" << i << ": " << results.plates[i].topNPlates.size() << " results";
      //std::cout << " -- Processing Time = " << results.plates[i].processing_time_ms << "ms.";
      std::cout << std::endl;

      if (results.plates[i].regionConfidence > 0)
        std::cout << "State ID: " << results.plates[i].region << " (" << results.plates[i].regionConfidence << "% confidence)" << std::endl;

      for (int k = 0; k < results.plates[i].topNPlates.size(); k++)
      {
        // Replace the multiline newline character with a dash
        std::string no_newline = results.plates[i].topNPlates[k].characters;
        std::replace(no_newline.begin(), no_newline.end(), '\n', '-');

        std::cout << "    - " << no_newline << "\t confidence: " << results.plates[i].topNPlates[k].overall_confidence;
        if (templatePattern.size() > 0 || results.plates[i].regionConfidence > 0)
          std::cout << "\t pattern_match: " << results.plates[i].topNPlates[k].matches_template;

        std::cout << std::endl;
        alpr::AlprPlate currentLicensePlate = results.plates[i].topNPlates[k];
        // The best license plate is the top one that matches the template.
        if ((bestLicensePlate == DEFAULT_LICENSE_PLATE) && currentLicensePlate.matches_template)
        {
          bestLicensePlate = currentLicensePlate.characters;
        }
      }
      // If there wasn't any template, the best license plate is the first top plate.
      if ((bestLicensePlate == DEFAULT_LICENSE_PLATE) && (results.plates[i].topNPlates.size() > 0))
      {
        bestLicensePlate = results.plates[i].topNPlates[0].characters;
      }
    }
  }

  return bestLicensePlate;
}