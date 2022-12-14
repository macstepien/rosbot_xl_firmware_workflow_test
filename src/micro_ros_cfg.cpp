/**
 * @file micro_ros_cfg.cpp
 * @author Maciej Kurcius
 * @brief 
 * @version 0.1
 * @date 2022-04-05
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include <micro_ros_cfg.h>

//ROS PUBLISHERS
rcl_publisher_t imu_publisher;
rcl_publisher_t motor_state_publisher;
rcl_publisher_t battery_state_publisher;
//ROS SUBSCRIPTIONS
rcl_subscription_t subscriber;
rcl_subscription_t motors_cmd_subscriber;
//ROS MESSAGES
sensor_msgs__msg__Imu imu_msg;
std_msgs__msg__String msgs;
std_msgs__msg__Float32MultiArray motors_cmd_msg;
sensor_msgs__msg__JointState motors_response_msg;
sensor_msgs__msg__BatteryState battery_state_msg;
//ROS
rclc_executor_t executor;
rclc_support_t support;
rcl_allocator_t allocator;
rcl_node_t node;
rcl_timer_t timer;
uRosFunctionStatus ping_agent_status;
//REST
extern FirmwareModeTypeDef firmware_mode;

void ErrorLoop(void){
  while (1) {
    if(firmware_mode == fw_debug) Serial.printf("In error loop");
    SetRedLed(Toggle);
    SetGreenLed(Off);
    delay(1000);
  }
}

uRosFunctionStatus uRosPingAgent(void){
  if(rmw_uros_ping_agent(AGENT_RECONNECTION_TIMEOUT, AGENT_RECONNECTION_ATTEMPTS) == RMW_RET_OK)
      return Ok;
  else
    return Error;  //if false
}

uRosFunctionStatus uRosPingAgent(uint8_t arg_timeout, uint8_t arg_attempts){
  if(rmw_uros_ping_agent((int)arg_timeout, arg_attempts) == RMW_RET_OK)
      return Ok;
    else
      return Error;  //if false
}

uRosFunctionStatus uRosLoopHandler(uRosFunctionStatus arg_agent_ping_status){
  static uRosEntitiesStatus entities_status = NotCreated;
  if(arg_agent_ping_status == Ok){
    if(entities_status != Created){
      entities_status = uRosCreateEntities();
      return Pending;
    }
    else{
      if(entities_status == Created)
      rclc_executor_spin_some(&executor, RCL_MS_TO_NS(100));
      return Ok;
    }
  }
  else{
    if(entities_status != Destroyed)
      entities_status = uRosDestroyEntities();
    return Error;
  }
}

void uRosMotorsCmdCallback(const void *arg_input_message){
  static double setpoint[] = {0,0,0,0};
  static std_msgs__msg__Float32MultiArray* setpoint_msg;
  setpoint_msg = (std_msgs__msg__Float32MultiArray*) arg_input_message;
  if(setpoint_msg->data.size == 4){
    for(uint8_t i = 0; i < setpoint_msg->data.size; i++){
      setpoint[i] = (double)setpoint_msg->data.data[i];
    }
  }
  xQueueSendToFront(SetpointQueue, (void*) setpoint, (TickType_t) 0);
}

void uRosTimerCallback(rcl_timer_t *arg_timer, int64_t arg_last_call_time) {
  RCLC_UNUSED(arg_last_call_time);
  static imu_queue_t queue_imu;
  static motor_state_queue_t motor_state_queue;
  static battery_state_queue_t battery_state_queue;
  if (arg_timer != NULL) {
    //QOS default
    if(xQueueReceive(BatteryStateQueue, &battery_state_queue, (TickType_t)0) == pdPASS){
      if(rmw_uros_epoch_synchronized()){
        battery_state_msg.header.stamp.sec = rmw_uros_epoch_millis()/1000;
        battery_state_msg.header.stamp.nanosec = rmw_uros_epoch_nanos();
      }
      battery_state_msg.voltage = battery_state_queue.voltage;
      battery_state_msg.temperature = battery_state_queue.temperature;
      battery_state_msg.current = battery_state_queue.current;
      battery_state_msg.charge = battery_state_queue.charge_current;
      battery_state_msg.capacity = battery_state_queue.capacity;
      battery_state_msg.design_capacity = battery_state_queue.design_capacity;
      battery_state_msg.percentage = battery_state_queue.percentage;
      battery_state_msg.power_supply_status = battery_state_queue.status;
      battery_state_msg.power_supply_health = battery_state_queue.health;
      battery_state_msg.power_supply_technology = battery_state_queue.technology;
      battery_state_msg.present = battery_state_queue.present;
      RCSOFTCHECK(rcl_publish(&battery_state_publisher, &battery_state_msg, NULL));
    }
    //QOS best effort
    if(xQueueReceive(MotorStateQueue, &motor_state_queue, (TickType_t) 0) == pdPASS){
      if(rmw_uros_epoch_synchronized()){
        motors_response_msg.header.stamp.sec = rmw_uros_epoch_millis()/1000;
        motors_response_msg.header.stamp.nanosec = rmw_uros_epoch_nanos();
      }
      motors_response_msg.velocity.data = motor_state_queue.velocity;
      motors_response_msg.position.data = motor_state_queue.positon;
      RCSOFTCHECK(rcl_publish(&motor_state_publisher, &motors_response_msg, NULL));
    }
    //QOS best effort
    if(xQueueReceive(ImuQueue, &queue_imu, (TickType_t) 0) == pdPASS){
      if(rmw_uros_epoch_synchronized()){
        imu_msg.header.stamp.sec = rmw_uros_epoch_millis()/1000;
        imu_msg.header.stamp.nanosec = rmw_uros_epoch_nanos();
      }
      imu_msg.header.frame_id.data = (char *) "imu";
      imu_msg.orientation.x = queue_imu.Orientation[0];
      imu_msg.orientation.y = queue_imu.Orientation[1];
      imu_msg.orientation.z = queue_imu.Orientation[2];
      imu_msg.orientation.w = queue_imu.Orientation[3];
      imu_msg.angular_velocity.x = queue_imu.AngularVelocity[0];
      imu_msg.angular_velocity.y = queue_imu.AngularVelocity[1];
      imu_msg.angular_velocity.z = queue_imu.AngularVelocity[2];
      imu_msg.linear_acceleration.x = queue_imu.LinearAcceleration[0];
      imu_msg.linear_acceleration.y = queue_imu.LinearAcceleration[1];
      imu_msg.linear_acceleration.z = queue_imu.LinearAcceleration[2];
      RCSOFTCHECK(rcl_publish(&imu_publisher, &imu_msg, NULL));
    }
  }
}

uRosEntitiesStatus uRosCreateEntities(void){
  uint8_t ros_msgs_cnt = 0;
  /*===== ALLCOATE MEMORY FOR MSGS =====*/
  MotorsResponseMsgInit(&motors_response_msg);
  MotorsCmdMsgInit(&motors_cmd_msg);
  allocator = rcl_get_default_allocator();
  // create init_options
  RCCHECK(rclc_support_init(&support, 0, NULL, &allocator))
  // create node
  RCCHECK(rclc_node_init_default(&node, NODE_NAME, "", &support));
  /*===== INIT TIMERS =====*/
  RCCHECK(rclc_timer_init_default(&timer, &support, RCL_MS_TO_NS(10),
                                  uRosTimerCallback));
  ros_msgs_cnt++;
  if(firmware_mode == fw_debug) Serial.printf("Created timer\r\n");
  /*===== INIT SUBSCRIBERS ===== */
  RCCHECK(rclc_subscription_init_best_effort(
      &motors_cmd_subscriber, &node, ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Float32MultiArray),
      "_motors_cmd"));
  ros_msgs_cnt++;
  if(firmware_mode == fw_debug) Serial.printf("Created 'motors_cmd' subscriber\r\n");
  /*===== INIT PUBLISHERS ===== */
  //IMU
  RCCHECK(rclc_publisher_init_best_effort(
      &imu_publisher, &node, ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs, msg, Imu),
      "_imu/data_raw"));
  // ros_msgs_cnt++;
  if(firmware_mode == fw_debug) Serial.printf("Created 'sensor_msgs/Imu' publisher.\r\n");
  //MOTORS RESPONSE
  RCCHECK(rclc_publisher_init_best_effort(
      &motor_state_publisher, &node, ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs, msg, JointState),
      "_motors_response"));
  // ros_msgs_cnt++;
  if(firmware_mode == fw_debug) Serial.printf("Created 'motors_response' publisher.\r\n");
  //BATTERY STATE
  RCCHECK(rclc_publisher_init_best_effort(
      &battery_state_publisher, &node, ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs, msg, BatteryState),
      "battery_state"));
  // ros_msgs_cnt++;
  if(firmware_mode == fw_debug) Serial.printf("Created 'battery_state' publisher.\r\n");
  // create executor
  RCCHECK(rclc_executor_init(&executor, &support.context, ros_msgs_cnt, &allocator));
  RCCHECK(rclc_executor_add_timer(&executor, &timer));
  RCCHECK(rclc_executor_add_subscription(&executor, &motors_cmd_subscriber, &motors_cmd_msg,
                                      &uRosMotorsCmdCallback, ON_NEW_DATA));                                 
  if(firmware_mode == fw_debug) Serial.printf("Executor started\r\n");
  RCCHECK(rmw_uros_sync_session(1000));
  if(firmware_mode == fw_debug) Serial.printf("Clocks synchronised\r\n");
  return Created;
}

uRosEntitiesStatus uRosDestroyEntities(void){
  rcl_publisher_fini(&imu_publisher, &node);
  rcl_publisher_fini(&motor_state_publisher, &node);
  rcl_publisher_fini(&battery_state_publisher, &node);
	rcl_node_fini(&node);
	rclc_executor_fini(&executor);
	rcl_timer_fini(&timer);
	rclc_support_fini(&support);
  return Destroyed;
}

void MotorsResponseMsgInit(sensor_msgs__msg__JointState * arg_message){
  static rosidl_runtime_c__String msg_name_tab[MOT_RESP_MSG_LEN];
  static double msg_data_tab[3][MOT_RESP_MSG_LEN];
  char* frame_id = (char*)"motors_response";
  arg_message->position.data = msg_data_tab[0];
  arg_message->position.capacity = arg_message->position.size = MOT_RESP_MSG_LEN;
  arg_message->velocity.data = msg_data_tab[1];
  arg_message->velocity.capacity = arg_message->velocity.size = MOT_RESP_MSG_LEN;
  arg_message->effort.data = msg_data_tab[2];
  arg_message->effort.capacity = arg_message->effort.size = MOT_RESP_MSG_LEN;
  arg_message->header.frame_id.data = frame_id;
  arg_message->header.frame_id.capacity = arg_message->header.frame_id.size = strlen((const char*)frame_id);
  msg_name_tab->capacity = msg_name_tab->size = MOT_RESP_MSG_LEN;
  msg_name_tab[0].data = (char*)"rear_right_wheel_joint";
  msg_name_tab[1].data = (char*)"rear_left_wheel_joint";
  msg_name_tab[2].data = (char*)"front_right_wheel_joint";
  msg_name_tab[3].data = (char*)"front_left_wheel_joint";
  for(uint8_t i = 0; i < MOT_RESP_MSG_LEN; i++){
      msg_name_tab[i].capacity = msg_name_tab[i].size = strlen(msg_name_tab[i].data);
  }
  arg_message->name.capacity = arg_message->name.size = MOT_RESP_MSG_LEN;
  arg_message->name.data = msg_name_tab;
}

void MotorsCmdMsgInit(std_msgs__msg__Float32MultiArray* arg_message){
  static float data[MOT_CMD_MSG_LEN] = {0,0,0,0};
  arg_message->data.capacity = MOT_CMD_MSG_LEN;
  arg_message->data.size = MOT_CMD_MSG_LEN;
  arg_message->data.data = (float*)data;
}