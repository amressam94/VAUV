// Copyright 2021 VorteX-co
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "../../../include/VDVL/serial_node.hpp"
#include <boost/algorithm/string.hpp>
#include <boost/bind.hpp>
#include <boost/date_time.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/thread.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "../../../include/VDVL/serial_port.hpp"
#include "custom_ros_interfaces/msg/dvl.hpp"
#include "rclcpp/rclcpp.hpp"
VDVL::SerialNode::SerialNode()
: Node("serial_node")
{
  this->declare_parameter<std::string>("portName", "/dev/ttyUSB0");
  this->get_parameter("portName", _portName);
  this->declare_parameter<int64_t>("baudRate", 115200);
  this->get_parameter("baudRate", _baudRate);
  std::string topic = "serial_packet";
  _publisher =
    this->create_publisher<custom_ros_interfaces::msg::DVL>(topic, 10);
}
void VDVL::SerialNode::Create(boost::asio::io_service & ios)
{
  /* *******************************************************
   * Method for creating a SerialPort instant
   * openning the serial Port
   * setting the onRead callback
   * ******************************************************* */
  try {
    _serial.reset(new VDVL::SerialPort(ios, _portName));
    _serial->Open(boost::bind(&VDVL::SerialNode::OnRead, this, _1, _2));
  } catch (const std::exception & e) {
    std::cout << e.what() << std::endl;
  }
}
void VDVL::SerialNode::OnPublish()
{
  /* *************************************************
   * Publishing callback
   * ************************************************/
  boost::mutex::scoped_lock lock(_rawDataMutex);
  std::cout << "OnPublish" << std::endl;
  auto packet = custom_ros_interfaces::msg::DVL();
  packet.time = _numerics[0];
  packet.vx = _numerics[1];
  packet.vy = _numerics[2];
  packet.vz = _numerics[3];
  packet.fom = _numerics[4];
  packet.altitude = _numerics[5];
  packet.status = _status;
  packet.valid = _valid;
  _publisher->publish(packet);
}
void VDVL::SerialNode::OnParse()
{
  /* *****************************************************
   * Extracing the data from the buffer
   * ****************************************************** */
  boost::mutex::scoped_lock lock(_rawDataMutex);
  char firstLetter = _rawData.at(0);
  if (firstLetter == 'w') {
    if (_rawData.length() < 3) {
      std::cout << "sentance is too short " << std::endl;
    } else {
      std::cout << _rawData << std::endl;
      int arr[8];
      int i;
      size_t found = 0;
      for (i = 0; i < 8; i++) {
        found = _rawData.find(",", found + 1);
        arr[i] = found;
      }
      std::string time = _rawData.substr(arr[0] + 1, arr[1] - arr[0] - 1);
      _numerics.push_back(std::atof(time.c_str()));  // from string to float
      std::string VX = _rawData.substr(arr[1] + 1, arr[2] - arr[1] - 1);
      _numerics.push_back(std::atof(VX.c_str()));
      std::string VY = _rawData.substr(arr[2] + 1, arr[3] - arr[2] - 1);
      _numerics.push_back(std::atof(VY.c_str()));
      std::string VZ = _rawData.substr(arr[3] + 1, arr[4] - arr[3] - 1);
      _numerics.push_back(std::atof(VZ.c_str()));
      std::string FOM = _rawData.substr(arr[4] + 1, arr[5] - arr[4] - 1);
      _numerics.push_back(std::atof(FOM.c_str()));
      std::string ALTITUDE = _rawData.substr(arr[5] + 1, arr[6] - arr[5] - 1);
      _numerics.push_back(std::atof(ALTITUDE.c_str()));
      std::string v = _rawData.substr(arr[6] + 1, arr[7] - arr[6] - 1);
      if (v[0] == 'y') {
        _valid = true;
      } else {
        _valid = false;
      }
      std::string s = _rawData.substr(arr[7] + 1, 1);
      char sc = s[0];
      if (sc == '1') {
        _status = true;
      } else {
        _status = false;
      }
      // posting the OnPublish callback to the I/O service to
      // be picked up be a worker thread.
      _serial->get_serial_port().get_io_service().post(
        boost::bind(&VDVL::SerialNode::OnPublish, this));
    }
  } else {
    std::cout << "Invalid format / Incomplete read" << std::endl;
  }
}
void VDVL::SerialNode::OnRead(
  std::vector<unsigned char> & buffer, size_t bytesRead)
{
  /* **********************************************************
   * OnRead callback is invoked to handle the received string buffer
   * before parsing.
   * ********************************************************** */
  boost::mutex::scoped_lock lock(_rawDataMutex);
  std::string tmpBuff(buffer.begin(), buffer.begin() + bytesRead);
  std::string EndByte1 = "\n";
  std::string EndByte2 = "\r\n";
  // removing the delimiters
  tmpBuff.erase(std::remove(tmpBuff.begin(), tmpBuff.end(), '\r'),
    tmpBuff.end());
  tmpBuff.erase(std::remove(tmpBuff.begin(), tmpBuff.end(), '\n'),
    tmpBuff.end());
  // setting the  _rawData string to be used by the OnPaser method
  _rawData = tmpBuff;
  // Posting the OnParser callback to be serviced ASAP by the I/O service
  // the call will return imedaitely and a ready worker thread will pickup
  // its execution
  _serial->get_serial_port().get_io_service().post(
    boost::bind(&SerialNode::OnParse, this));
}