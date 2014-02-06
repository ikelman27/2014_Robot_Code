#include "WPILib.h"
#include <math.h>
#include "Vision/HSLImage.h"
#define X_IMAGE_RES 320		//X Image resolution in pixels, should be 160, 320 or 640
#define VIEW_ANGLE 48		//Axis 206 camera
//#define VIEW_ANGLE 43.5  //Axis M1011 camera
#define PI 3.141592653

//Score limits used for target identification
#define RECTANGULARITY_LIMIT 60
#define ASPECT_RATIO_LIMIT 75
#define X_EDGE_LIMIT 40
#define Y_EDGE_LIMIT 60

//Minimum area of particles to be considered
#define AREA_MINIMUM 500

//Edge profile constants used for hollowness score calculation
#define XMAXSIZE 24
#define XMINSIZE 24
#define YMAXSIZE 24
#define YMINSIZE 48
const double xMax[XMAXSIZE] = {1, 1, 1, 1, .5, .5, .5, .5, .5, .5, .5, .5, .5, .5, .5, .5, .5, .5, .5, .5, 1, 1, 1, 1};
const double xMin[XMINSIZE] = {.4, .6, .1, .1, .1, .1, .1, .1, .1, .1, .1, .1, .1, .1, .1, .1, .1, .1, .1, .1, .1, .1, 0.6, 0};
const double yMax[YMAXSIZE] = {1, 1, 1, 1, .5, .5, .5, .5, .5, .5, .5, .5, .5, .5, .5, .5, .5, .5, .5, .5, 1, 1, 1, 1};
const double yMin[YMINSIZE] = {.4, .6, .05, .05, .05, .05, .05, .05, .05, .05, .05, .05, .05, .05, .05, .05, .05, .05, .05, .05, .05, .05,
								.05, .05, .05, .05, .05, .05, .05, .05, .05, .05, .05, .05, .05, .05, .05, .05, .05, .05, .05, .05, .05, .05,
								.05, .05, .6, 0};
const double RectHotGoalRatio = 0.95;

class BuiltinDefaultCode : public IterativeRobot {


	// Motor controllers
	Talon *left_1;
	Talon *left_2;
	Talon *right_1;
	Talon *right_2;
	Victor *zoidberg;
		
	// Solenoids
	Solenoid *shiftRight;
	Solenoid *shiftLeft;
	Solenoid *passingPiston;
	
	// Gyro
	Gyro *mainGyro;
	
	// Encoders
	Encoder *leftEncoder;
	Encoder *rightEncoder;
	Encoder *shooterEncoder; // Not sure if this is actually needed I think so unless we are using a limit switch 
	
	// Axis Camera
	AxisCamera *camera;
	
	// Limit switches
	DigitalInput *limitSwitch;

public:
	BuiltinDefaultCode(void) {
		printf("BuiltinDefaultCode Constructor Started\n");
		
		left_1  = new Talon(1);
		left_2  = new Talon(2);
		right_1 = new Talon(3);
		right_2 = new Talon(4);
		zoidberg = new Victor(5);
		
		shiftRight = new Solenoid(1);
		shiftLeft  = new Solenoid(2);
		
		mainGyro = new Gyro(5);
		
		leftEncoder    = new Encoder(6, 8); // Second int is a placeholder to fix an error with the code (Encoder takes 2 ints)
		rightEncoder   = new Encoder(7, 9); // Same here
		shooterEncoder = new Encoder(10, 11); // Also same here
		
		gamePad = new Joystick(1);
		
		limitSwitch = new DigitalInput(1); // 1 is a placeholder for the Digital Input location

		// Acquire the Driver Station object
		m_ds = DriverStation::GetInstance();
		m_priorPacketNumber = 0;
		m_dsPacketsReceivedInCurrentSecond = 0;

		// Initialize counters to record the number of loops completed in autonomous and teleop modes
		m_autoPeriodicLoops = 0;
		m_disabledPeriodicLoops = 0;
		m_telePeriodicLoops = 0;

		printf("BuiltinDefaultCode Constructor Completed\n");
	}
	
	
	/********************************** Init Routines *************************************/


	void RobotInit(void) {
		// Actions which would be performed once (and only once) upon initialization of the
		// robot would be put here.
		shiftLeft->Set(false); //low gear
		shiftRight->Set(false); //low gear
		passingPiston->Set(false); //retracted
		printf("RobotInit() completed.\n");
	}
	
	void DisabledInit(void) {
		m_disabledPeriodicLoops = 0; // Reset the loop counter for disabled mode
	}

	void AutonomousInit(void) {
		m_autoPeriodicLoops = 0; // Reset the loop counter for autonomous mode
		mainGyro->Reset();       // Assures us gyro starts at 0 degrees
	}

	void TeleopInit(void) {
		m_telePeriodicLoops = 0; // Reset the loop counter for teleop mode
		m_dsPacketsReceivedInCurrentSecond = 0; // Reset the number of dsPackets in current second
	}

	/********************************** Periodic Routines *************************************/

	void DisabledPeriodic(void) {
	}

	void AutonomousPeriodic(void) 
	{
		initialShot(1); // 1 is a temporary value.
		centerRobot();
		seekAndDestroy();
		reposition();
		shoot();
		centerRobot();
		seekAndDestroy();
		reposition();
		shoot();
	}

	/********************************* Teleop methods *****************************************/
	void motorControlLeft(float speed) 
	{
		left_1->SetSpeed(speed);
		left_2->SetSpeed(speed);
	}

	void motorControlRight(float speed)
	{
		right_1->SetSpeed(speed);
		right_2->SetSpeed(speed);
	}

	void ShiftHigh(void) 
	{
		// shiftRight->Get() return value: false is low gear, true is high gear
		if(!(shiftRight->Get())) 
		{
			shiftRight->Set(true);
			shiftLeft->Set(true);
		}
	}

	void ShiftLow(void) 
	{
		// shiftRight->Get() return value: false is low gear, true is high gear
		if(shiftRight->Get()) {
			shiftRight->Set(false);
			shiftLeft->Set(false);
		}
	}

	void TeleopPeriodic(void)
	{
		bool flag = true; // Flag object initial declaration to ensure passing piston toggle works properly

		float leftStick  = gamePad->GetRawAxis(4);
		float rightStick = gamePad->GetRawAxis(2);
		
		bool rightBumper = gamePad->GetRawButton(6);
		bool leftBumper  = gamePad->GetRawButton(5);
		
		bool buttonA     = gamePad->GetRawButton(2);
		
		if(rightBumper || leftBumper) 
		{
			if(rightBumper && !leftBumper) 
			{
				ShiftHigh();
			}
			else if(leftBumper && !rightBumper) 
			{
				ShiftLow();
			}
		}
		else if(buttonA && flag)
		{
			flag = false;
			passingPiston->Set(!(passingPiston->Get()));	// flag reset to false while button a is not held to avoid continuous switching while a button is held
		}
		else if(!buttonA)
		{
				flag = true;
				motorControlLeft(leftStick);
				motorControlRight(rightStick);
		
			}
			
			motorControlLeft(leftStick);
			motorControlRight(rightStick); // motor speed declarations done at the end to ensure watchdog is continually updated.
	} 


	/********************************** Continuous Routines *************************************/
	
	int identifyBall(void)
	{
		
		Threshold threshold(60, 100, 90, 255, 20, 255);	//HSV threshold criteria, ranges are in that order ie. Hue is 60-100
		ParticleFilterCriteria2 criteria[] = {
				{IMAQ_MT_AREA, AREA_MINIMUM, 65535, false, false}
		};												//Particle filter criteria, used to filter out small particles
		// AxisCamera &camera = AxisCamera::GetInstance();	//To use the Axis camera uncomment this line
            /**
             * Do the image capture with the camera and apply the algorithm described above. This
             * sample will either get images from the camera or from an image file stored in the top
             * level directory in the flash memory on the cRIO. The file name in this case is "testImage.jpg"
             */
			ColorImage *image;
			//image = new RGBImage("/testImage.jpg");		// get the sample image from the cRIO flash

			camera.GetImage(image);				//To get the images from the camera comment the line above and uncomment this one
			BinaryImage *thresholdImage = image->ThresholdHSV(threshold);	// get just the green target pixels
			//thresholdImage->Write("/threshold.bmp");
			BinaryImage *convexHullImage = thresholdImage->ConvexHull(false);  // fill in partial and full rectangles
			//convexHullImage->Write("/ConvexHull.bmp");
			BinaryImage *filteredImage = convexHullImage->ParticleFilter(criteria, 1);	//Remove small particles
			//filteredImage->Write("Filtered.bmp");

			vector<ParticleAnalysisReport> *reports = filteredImage->GetOrderedParticleAnalysisReports();  //get a particle analysis report for each particle
			scores = new Scores[reports->size()];
			
			//Iterate through each particle, scoring it and determining whether it is a target or not
			for (unsigned i = 0; i < reports->size(); i++) {
				ParticleAnalysisReport *report = &(reports->at(i));
				
				scores[i].rectangularity = scoreRectangularity(report);
				scores[i].aspectRatioOuter = scoreAspectRatio(filteredImage, report, true);
				scores[i].aspectRatioInner = scoreAspectRatio(filteredImage, report, false);			
				scores[i].xEdge = scoreXEdge(thresholdImage, report);
				scores[i].yEdge = scoreYEdge(thresholdImage, report);
				
				if(scoreCompare(scores[i], false))
				{
					printf("particle: %d  is a High Goal  centerX: %f  centerY: %f \n", i, report->center_mass_x_normalized, report->center_mass_y_normalized);
					printf("Distance: %f \n", computeDistance(thresholdImage, report, false));
				} else if (scoreCompare(scores[i], true)) {
					printf("particle: %d  is a Middle Goal  centerX: %f  centerY: %f \n", i, report->center_mass_x_normalized, report->center_mass_y_normalized);
					printf("Distance: %f \n", computeDistance(thresholdImage, report, true));
				} else {
					printf("particle: %d  is not a goal  centerX: %f  centerY: %f \n", i, report->center_mass_x_normalized, report->center_mass_y_normalized);
				}
				printf("rect: %f  ARinner: %f \n", scores[i].rectangularity, scores[i].aspectRatioInner);
				printf("ARouter: %f  xEdge: %f  yEdge: %f  \n", scores[i].aspectRatioOuter, scores[i].xEdge, scores[i].yEdge);	
			}
			printf("\n");
			
			// be sure to delete images after using them
			delete filteredImage;
			delete convexHullImage;
			delete thresholdImage;
			delete image;
			
			//delete allocated reports and Scores objects also
			delete scores;
			delete reports;
		}
	}
	
	
		double computeDistance (BinaryImage *image, ParticleAnalysisReport *report, bool outer) {
			double rectShort, height;
			int targetHeight;
		
			imaqMeasureParticle(image->GetImaqImage(), report->particleIndex, 0, IMAQ_MT_EQUIVALENT_RECT_SHORT_SIDE, &rectShort);
			//using the smaller of the estimated rectangle short side and the bounding rectangle height results in better performance
			//on skewed rectangles
			height = min(report->boundingRect.height, rectShort);
			targetHeight = outer ? 29 : 21;
		
			return X_IMAGE_RES * targetHeight / (height * 12 * 2 * tan(VIEW_ANGLE*PI/(180*2)));
		}
		double scoreAspectRatio(BinaryImage *image, ParticleAnalysisReport *report, bool outer){
			double rectLong, rectShort, idealAspectRatio, aspectRatio;
			idealAspectRatio = outer ? (62/29) : (62/20);	//Dimensions of goal opening + 4 inches on all 4 sides for reflective tape
		
			imaqMeasureParticle(image->GetImaqImage(), report->particleIndex, 0, IMAQ_MT_EQUIVALENT_RECT_LONG_SIDE, &rectLong);
			imaqMeasureParticle(image->GetImaqImage(), report->particleIndex, 0, IMAQ_MT_EQUIVALENT_RECT_SHORT_SIDE, &rectShort);
		
			//Divide width by height to measure aspect ratio
			if(report->boundingRect.width > report->boundingRect.height){
				//particle is wider than it is tall, divide long by short
				aspectRatio = 100*(1-fabs((1-((rectLong/rectShort)/idealAspectRatio))));
			} else {
				//particle is taller than it is wide, divide short by long
				aspectRatio = 100*(1-fabs((1-((rectShort/rectLong)/idealAspectRatio))));
			}
			return (max(0, min(aspectRatio, 100)));		//force to be in range 0-100
		}
		
	void initialShot(int x)
	{
		while(shooterEncoder->Get() < x)
		{
			motorControlLeft(1.0); // Move forward for set length determined by encoders position
			motorControlRight(1.0);
		}
		shoot();
	}

	bool centerRobot(void) // 1
	{
		while(!identifyBall()) // Assumes identifyBall returns true if ball is centered
		{
			motorControlLeft(-0.5); // This method needs to take an int 0,1,2,3 from the identifyBall method based off the location of the ball
									// Where 0 means the ball is too the left, 1 the ball is centered, and 2 the ball is to the right, 3, the ball is not onscreen
			motorControlRight(0.5);
		}
		return true; // Temporary value
	}

	void seekAndDestroy(void) // 2
	{
		float x = 1.0; // Temporary value
		while(!limitSwitch->Get())    // There will be a limiter switch in the catapault mechanism the robot should check to see if it captured the ball with this.
		{
			if(centerRobot())
			{
				motorControlLeft(x);  // x is the max value for the motors.
				motorControlRight(x); // This method should call the turn method based off of input from the find ball method, adjusting the angle first
									  // Centering the robot on the ball then driving to the ball 
			}
		}
	}

	void reposition(void) // 3
	{
		turn(0); // Faces the robot in the initial orientation
	}

	void shoot(void) // 4
	{
		
	}

	void turn(int x)
	{
		leftEncoder->Reset();
		rightEncoder->Reset();
		if(x <= 0)
		{
			while(mainGyro->GetAngle() > x) // encoder check with gyro this code is psuedo for the sake of outline not actual content of the while loop 
								// Gyro needs to have an accepted angle value. +- 5 degrees? or so most likely less, the accepted angle must be based off the distance to the ball we can discuss this today.
			{
				motorControlLeft(-0.5);
				motorControlRight(0.5);
			}
		}
		else
		{
			while(mainGyro->GetAngle() < x) // Encoder -> turn radius shit here check with gyro
			{
				motorControlLeft(0.5);
				motorControlRight(-0.5);
			}
		}
	}
	
	void DisabledContinuous(void)
	{

	}
        
	void AutonomousContinuous(void)        
	{

	}

	void TeleopContinuous(void)
	{
			
	}
};

START_ROBOT_CLASS(BuiltinDefaultCode);
