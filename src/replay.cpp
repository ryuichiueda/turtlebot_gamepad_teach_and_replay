#include "ros/ros.h"
#include "Event.h"
#include "Episodes.h"
#include "ParticleFilter.h"
#include <iostream>
#include <fstream>
#include <signal.h>
#include <rosbag/bag.h>
#include <rosbag/view.h>
#include <ros/package.h> 
#include "std_srvs/Trigger.h"
#include "geometry_msgs/Twist.h"
/* #include "raspimouse_ros_2/TimedMotion.h" */
#include "turtlebot_gamepad_training_replay/DepthSensorValues.h"
#include "turtlebot_gamepad_training_replay/ButtonValues.h"
#include "turtlebot_gamepad_training_replay/Event.h"
#include "ParticleFilter.h"
#include "turtlebot_gamepad_training_replay/PFoEOutput.h"
using namespace ros;

Episodes ep;
ParticleFilter pf(1000,&ep);

Observation sensor_values;

NodeHandle *np;
int sum_center = 0;

bool on = false;
bool bag_read = false;

void buttonCallback(const turtlebot_gamepad_training_replay::ButtonValues::ConstPtr& msg)
{
    on = msg->replay;
}

void sensorCallback(const turtlebot_gamepad_training_replay::DepthSensorValues::ConstPtr& msg)
{
	sensor_values.setValues(msg->left_side,msg->left_center,msg->right_center,msg->right_side);
	sum_center = msg->sum_center;
}

/*
void on_shutdown(int sig)
{
	ros::ServiceClient motor_off = np->serviceClient<std_srvs::Trigger>("motor_off");
	std_srvs::Trigger t;
	motor_off.call(t);

	shutdown();
}
*/

void readEpisodes(string file)
{
	ep.reset();

	rosbag::Bag bag1(file, rosbag::bagmode::Read);

	vector<std::string> topics;
	topics.push_back("/event");
	
	rosbag::View view(bag1, rosbag::TopicQuery(topics));

	double start = view.getBeginTime().toSec() + 5.0; //discard first 5 sec
	double end = view.getEndTime().toSec() - 5.0; //discard last 5 sec
	for(auto i : view){
	        auto s = i.instantiate<turtlebot_gamepad_training_replay::Event>();

		Observation obs(s->left_side,s->left_center,s->right_center,s->right_side);
		Action a = {s->linear_x,s->angular_z};
		Event e(obs,a,0.0);
		e.time = i.getTime();

		if(e.time.toSec() < start)
			continue;

		ep.append(e);

		if(e.time.toSec() > end)
			break;
	}
}

int main(int argc, char **argv)
{
	init(argc,argv,"go_around");
	NodeHandle n;
	np = &n;

	Subscriber sub = n.subscribe("DepthSensor", 1, sensorCallback);
	Subscriber sub_b = n.subscribe("joy", 1, buttonCallback);
	Publisher cmdvel = n.advertise<geometry_msgs::Twist>("cmd_vel", 1);
	Publisher pfoe_out = n.advertise<turtlebot_gamepad_training_replay::PFoEOutput>("pfoe_out", 100);

	/* signal(SIGINT, on_shutdown); */

	/* motor_on.waitForExistence(); */

	geometry_msgs::Twist msg;
	pf.init();
	Rate loop_rate(10);
	Action act = {0.0,0.0};
	while(ok()){
		if(not on){
			cout << "idle" << endl;
			bag_read = false;
			spinOnce();
			loop_rate.sleep();
			continue;
		}else if(not bag_read){
			string bagfile;
			n.getParam("/current_bag_file", bagfile);
			readEpisodes(bagfile);
			bag_read = true;
			pf.init();
			spinOnce();
			loop_rate.sleep();
			continue;
		}
		turtlebot_gamepad_training_replay::PFoEOutput out;

		act = pf.sensorUpdate(&sensor_values, &act, &ep, &out);
		msg.linear.x = act.linear_x;
		out.linear_x = act.linear_x;
		msg.angular.z = act.angular_z;
		out.angular_z = act.angular_z;

		out.left_side = sensor_values.ls;
		out.left_center = sensor_values.lc;
		out.right_center = sensor_values.rc;
		out.right_side = sensor_values.rs;

		cmdvel.publish(msg);
		pfoe_out.publish(out);
		pf.motionUpdate(&ep);

		spinOnce();
		loop_rate.sleep();
	}
	return 0;
}
