/*******************************************************************************
* Copyright 2016 ROBOTIS CO., LTD.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

/* Authors: Taehoon Lim (Darby) */

#include "dynamixel_workbench_controllers/torque_control.h"

TorqueControl::TorqueControl()
    :node_handle_(""),
     dxl_cnt_(2)
{
  std::string device_name   = node_handle_.param<std::string>("device_name", "/dev/ttyUSB0");
  uint32_t dxl_baud_rate    = node_handle_.param<int>("baud_rate", 3000000);

  p_gain_ = node_handle_.param<float>("p_gain", 0.003);
  d_gain_ = node_handle_.param<float>("d_gain", 0.00002);

  dxl_id_[PAN] = node_handle_.param<int>("pan_id", 1);
  dxl_id_[TILT] = node_handle_.param<int>("tilt_id", 2);

  dxl_wb_ = new DynamixelWorkbench;

  dxl_wb_->begin(device_name.c_str(), dxl_baud_rate);
  for (int index = 0; index < dxl_cnt_; index++)
  {
    uint16_t get_model_number;
    dxl_wb_->ping(dxl_id_[index], &get_model_number);
  }

  initMsg();

  for (int index = 0; index < dxl_cnt_; index++)
  {
    dxl_wb_->itemWrite(dxl_id_[index], "Torque_Enable", 0);
    dxl_wb_->itemWrite(dxl_id_[index], "Operating_Mode", X_SERIES_CURRENT_CONTROL_MODE);
    dxl_wb_->itemWrite(dxl_id_[index], "Torque_Enable", 1);
  }

  dxl_wb_->addSyncWrite("Goal_Current");
  dxl_wb_->addSyncRead("Present_Position");

  initPublisher();
  initServer();
}

TorqueControl::~TorqueControl()
{
  for (int index = 0; index < 2; index++)
    dxl_wb_->itemWrite(dxl_id_[index], "Torque_Enable", 0);

  ros::shutdown();
}

void TorqueControl::initMsg()
{
  printf("-----------------------------------------------------------------------\n");
  printf("  dynamixel_workbench controller; torque control example               \n");
  printf("-----------------------------------------------------------------------\n");
  printf("\n");

  for (int index = 0; index < dxl_cnt_; index++)
  {
    printf("MODEL   : %s\n", dxl_wb_->getModelName(dxl_id_[index]));
    printf("ID      : %d\n", dxl_id_[index]);
    printf("\n");
  }
  printf("-----------------------------------------------------------------------\n");
}

void TorqueControl::initPublisher()
{
  dynamixel_state_list_pub_ = node_handle_.advertise<dynamixel_workbench_msgs::DynamixelStateList>("dynamixel_state", 10);
}

void TorqueControl::initServer()
{
  joint_command_server_ = node_handle_.advertiseService("joint_command", &TorqueControl::jointCommandMsgCallback, this);
}

void TorqueControl::dynamixelStatePublish()
{
  dynamixel_workbench_msgs::DynamixelState     dynamixel_state[dxl_cnt_];
  dynamixel_workbench_msgs::DynamixelStateList dynamixel_state_list;

  for (int index = 0; index < dxl_cnt_; index++)
  {
    dynamixel_state[index].model_name          = std::string(dxl_wb_->getModelName(dxl_id_[index]));
    dynamixel_state[index].id                  = dxl_id_[index];
    dynamixel_state[index].torque_enable       = dxl_wb_->itemRead(dxl_id_[index], "Torque_Enable");
    dynamixel_state[index].present_position    = dxl_wb_->itemRead(dxl_id_[index], "Present_Position");
    dynamixel_state[index].present_velocity    = dxl_wb_->itemRead(dxl_id_[index], "Present_Velocity");
    dynamixel_state[index].present_current     = dxl_wb_->itemRead(dxl_id_[index], "Present_Current");
    dynamixel_state[index].goal_position       = dxl_wb_->itemRead(dxl_id_[index], "Goal_Position");
    dynamixel_state[index].goal_velocity       = dxl_wb_->itemRead(dxl_id_[index], "Goal_Velocity");
    dynamixel_state[index].goal_current        = dxl_wb_->itemRead(dxl_id_[index], "Goal_Current");
    dynamixel_state[index].moving              = dxl_wb_->itemRead(dxl_id_[index], "Moving");

    dynamixel_state_list.dynamixel_state.push_back(dynamixel_state[index]);
  }
  dynamixel_state_list_pub_.publish(dynamixel_state_list);
}

void TorqueControl::controlLoop()
{
  dynamixelStatePublish();
  gravityCompensation();
}

void TorqueControl::gravityCompensation()
{
  const float tilt_motor_mass = 0.082;
  const float gravity         = 9.8;
  const float link_length     = 0.018;

  int32_t error[2] = {0, 0};
  static int32_t pre_error[2] = {0, 0};
  float calc_torque[2] = {0.0, 0.0};
  int32_t goal_torque[2] = {0, 0};

  int32_t* present_position = dxl_wb_->syncRead("Present_Position");

  printf("present_position[PAN]  = %d  present_position[TILT] = %d\n", present_position[PAN], present_position[TILT]);

  error[PAN]  = goal_position_[PAN]  - present_position[PAN];
  error[TILT] = goal_position_[TILT] - present_position[TILT];

  calc_torque[PAN]  = p_gain_ * error[PAN] +
                      d_gain_ * ((error[PAN] - pre_error[PAN]) / 0.004);

  calc_torque[TILT] = p_gain_ * error[TILT] +
                      d_gain_ * ((error[TILT] - pre_error[TILT]) / 0.004) +
                      tilt_motor_mass * gravity * link_length * cos(dxl_wb_->convertValue2Radian(dxl_id_[TILT], present_position[TILT]));


  goal_torque[PAN]  = (int32_t)(dxl_wb_->convertTorque2Value(dxl_id_[PAN] , calc_torque[PAN]));
  goal_torque[TILT] = (int32_t)(dxl_wb_->convertTorque2Value(dxl_id_[TILT], calc_torque[TILT]));

  printf("goal_torque[PAN]  = %d  goal_torque[TILT] = %d\n", goal_torque[PAN], goal_torque[TILT]);

  dxl_wb_->syncWrite("Goal_Current", goal_torque);

  pre_error[PAN]  = error[PAN];
  pre_error[TILT] = error[TILT];

//  error[pan]  = motorPos_->des_pos.at(PAN)  - motorPos_->cur_pos.at(PAN);
//  error[tilt] = motorPos_->des_pos.at(TILT) - motorPos_->cur_pos.at(TILT);

//  torque[PAN]  = p_gain_ * error[PAN] +
//                 d_gain_ * ((error[PAN] - pre_error[PAN]) / 0.004);
//  torque[TILT] = p_gain_ * error[TILT] +
//                 d_gain_ * ((error[TILT] - pre_error[TILT]) / 0.004) +
//                 tilt_motor_mass * gravity * link_length * cos(convertValue2Radian((int32_t)motorPos_->cur_pos.at(TILT)));

//  setCurrent(convertTorque2Value(torque[PAN]), convertTorque2Value(torque[TILT]));

//  pre_error[PAN]  = error[PAN];
//  pre_error[TILT] = error[TILT];
}

bool TorqueControl::jointCommandMsgCallback(dynamixel_workbench_msgs::JointCommand::Request &req,
                                            dynamixel_workbench_msgs::JointCommand::Response &res)
{
  if (req.unit == "rad")
  {
    if (dxl_id_[PAN] == req.id)
      goal_position_[PAN] = dxl_wb_->convertRadian2Value(req.id, req.goal_position);
    else
      goal_position_[TILT] = dxl_wb_->convertRadian2Value(req.id, req.goal_position);
  }
  else if (req.unit == "raw")
  {
    if (dxl_id_[PAN] == req.id)
      goal_position_[PAN] = req.goal_position;
    else
      goal_position_[TILT] = req.goal_position;
  }
  else
  {
    if (dxl_id_[PAN] == req.id)
      goal_position_[PAN] = req.goal_position;
    else
      goal_position_[TILT] = req.goal_position;
  }

  res.result = true;

//  motorPos_->des_pos.clear();

//  if (req.unit == "rad")
//  {
//    motorPos_->des_pos.push_back(convertRadian2Value(req.pan_pos));
//    motorPos_->des_pos.push_back(convertRadian2Value(req.tilt_pos));
//  }
//  else if (req.unit == "raw")
//  {
//    motorPos_->des_pos.push_back(req.pan_pos);
//    motorPos_->des_pos.push_back(req.tilt_pos);
//  }
//  else
//  {
//    motorPos_->des_pos.push_back(req.pan_pos);
//    motorPos_->des_pos.push_back(req.tilt_pos);
//  }

//  res.pan_pos  = motorPos_->des_pos.at(PAN);
//  res.tilt_pos = motorPos_->des_pos.at(TILT);
}

int main(int argc, char **argv)
{
  // Init ROS node
  ros::init(argc, argv, "torque_control");
  TorqueControl torque_ctrl;

  ros::Rate loop_rate(250);

  while (ros::ok())
  {
    torque_ctrl.controlLoop();
    ros::spinOnce();
    loop_rate.sleep();
  }

  return 0;
}
