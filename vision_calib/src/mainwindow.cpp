#include "mainwindow.h"
#include "ui_vision_calib.h"

MainWindow::MainWindow(int robot_id, bool real_robot, QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
   ui->setupUi(this);
   setup();
   
   robot_id_ = robot_id;
   calibration_mode = draw_mode = false;
   img_calib_timer = new QTimer();
   interaction_timer = new QTimer();
   img_calib_ = new ImageCalibrator();
   ui->lb_robot_name->setStyleSheet("QLabel { color : red; }");
   ui->lb_robot_name->setText(QString("Calibration Mode for Robot ")+QString::number(robot_id_));
   connect(this,SIGNAL(addNewImage()),this,SLOT(addImageToScene()));
   ui->graphicsView->setHorizontalScrollBarPolicy ( Qt::ScrollBarAlwaysOff );
	ui->graphicsView->setVerticalScrollBarPolicy ( Qt::ScrollBarAlwaysOff );
	connect(img_calib_timer,SIGNAL(timeout()),this,SLOT(applyBinary()));
	connect(interaction_timer,SIGNAL(timeout()),this,SLOT(interactWithUser()));
   // Setup of ROS
   QString asd = "Vision_calib";
   asd.append(QString::number(robot_id));
   asd.append("_");
   asd.append(QString::number(std::time(0)));
   std::stringstream imreq_topic;
   std::stringstream imgtrans_topic;
   std::stringstream mirror_topic;
   std::stringstream vision_topic;
   std::stringstream image_topic;
   
   if(real_robot){
      // Setup ROS Node and pusblishers/subscribers in SIMULATOR
      imreq_topic << "/imgRequest";
      imgtrans_topic << "/camera";
      mirror_topic << "/mirrorConfig";
      vision_topic << "/visionHSVConfig";
      image_topic << "/imageConfig";
      
      if(robot_id>0){
         // Setup custom master
         QString robot_ip = QString(ROS_MASTER_IP)+QString::number(robot_id)
                            +QString(ROS_MASTER_PORT);
         ROS_WARN("ROS_MASTER_URI: '%s'",robot_ip.toStdString().c_str());
         setenv("ROS_MASTER_URI",robot_ip.toStdString().c_str(),1);
      } else ROS_WARN("ROS_MASTER_URI is localhost");
      
   } else {
      // Setup ROS Node and pusblishers/subscribers in REAL ROBOT
      imreq_topic << "minho_gazebo_robot" << std::to_string(robot_id) << "/imgRequest";
      imgtrans_topic << "minho_gazebo_robot" << std::to_string(robot_id) << "/camera";
      mirror_topic << "minho_gazebo_robot" << std::to_string(robot_id) << "/mirrorConfig";
      vision_topic << "minho_gazebo_robot" << std::to_string(robot_id) << "/visionHSVConfig";
      image_topic << "minho_gazebo_robot" << std::to_string(robot_id) << "/imageConfig";
   }
   

   //Initialize ROS
   int argc = 0;
   ros::init(argc, NULL, asd.toStdString().c_str(),ros::init_options::NoSigintHandler);
   _node_ = new ros::NodeHandle();
   it_ = new image_transport::ImageTransport(*_node_);
   imgreq_pub_ = _node_->advertise<imgRequest>(imreq_topic.str().c_str(),100);
   mirror_pub_ = _node_->advertise<mirrorConfig>(mirror_topic.str().c_str(),100);
   vision_pub_ = _node_->advertise<visionHSVConfig>(vision_topic.str().c_str(),100);
   image_pub_ = _node_->advertise<imageConfig>(image_topic.str().c_str(),100);
   
   omniVisionConf = _node_->serviceClient<minho_team_ros::requestOmniVisionConf>("requestOmniVisionConf");
   //Initialize image_transport subscriber
   image_sub_ = it_->subscribe(imgtrans_topic.str().c_str(),1,&MainWindow::display_image,this);
   
   
   //Request Current Configuration
   requestOmniVisionConf srv; 
   srv.request.request_node_name = asd.toStdString();
   if(omniVisionConf.call(srv)){
      ROS_INFO("Retrieved configuration from target robot.");
      img_calib_->mirrorConfigFromMsg(srv.response.mirrorConf);
      img_calib_->lutConfigFromMsg(srv.response.visionConf);
      img_calib_->imageConfigFromMsg(srv.response.imageConf);
      loadValuesOnTrackbars(img_calib_->getLabelConfiguration(static_cast<LABEL_t>(ui->combo_label->currentIndex())));
      loadMirrorValues(srv.response.mirrorConf);
      loadImageValues(srv.response.imageConf);
      
   } else ROS_ERROR("Failed to retrieve configuration from target robot.");
   
   interaction_timer->start(100);
}

MainWindow::~MainWindow()
{  
   ros::shutdown();
   delete ui;
}

void MainWindow::setup()
{
   scene_ = new QGraphicsScene();
   ui->graphicsView->setScene(scene_); 
   temp = Mat(480,480,CV_8UC3,Scalar(0,0,0));
}
   
   
void MainWindow::keyPressEvent(QKeyEvent *event)
{
   switch(event->key()){
      // Mode change
      case Qt::Key_R:{ //change mode to raw
         ui->combo_aqtype->setCurrentIndex(0);
         break;
      }
      case Qt::Key_W:{ //change mode to world
         ui->combo_aqtype->setCurrentIndex(2);
         break;
      }
      case Qt::Key_S:{ //change mode to segmented
         ui->combo_aqtype->setCurrentIndex(1);
         break;
      }
      case Qt::Key_M:{ //change mode to map
         ui->combo_aqtype->setCurrentIndex(3);
         break;
      }
      case Qt::Key_F:{ //feed - multiple images
         ui->radio_multiple->setChecked(true);
         on_bt_grab_clicked();
         break;
      }
      case Qt::Key_I:{ //increase fps
         ui->spin_framerate->setValue(ui->spin_framerate->value()+1);
         on_bt_grab_clicked();
         break;
      }
      case Qt::Key_D:{ //decrease fps
         ui->spin_framerate->setValue(ui->spin_framerate->value()-1);
         on_bt_grab_clicked();
         break;
      }
      case Qt::Key_G:{ //grab single image
         // Single image
         ui->radio_single->setChecked(true);
         on_bt_grab_clicked();
         break;
      }
      case Qt::Key_T:{ //turn class mode on or off
         if(!calibration_mode){
            calibration_mode = true;
            ui->lb_robot_name->setStyleSheet("QLabel { color : green; }");
            img_calib_timer->start(30);
            interaction_timer->stop();
         } else {
            calibration_mode = false;
            ui->lb_robot_name->setStyleSheet("QLabel { color : red; }");
            img_calib_timer->stop();
            interaction_timer->start(100);
         }
         ui->combo_aqtype->setCurrentIndex(0); // Raw mode when
         on_bt_grab_clicked();
         break;
      }
   }
}
void MainWindow::display_image(const sensor_msgs::ImageConstPtr& msg)
{
   cv_bridge::CvImagePtr recv_img = cv_bridge::toCvCopy(msg,"bgr8");
   temp = recv_img->image;
   image_ =  QImage( temp.data,
                 temp.cols, temp.rows,
                 static_cast<int>(temp.step),
                 QImage::Format_RGB888 );

   emit addNewImage();
}

void MainWindow::addImageToScene()
{
   if(!calibration_mode && !draw_mode){
      scene_->clear();
      scene_->addPixmap(QPixmap::fromImage(image_));
   }
}

void MainWindow::applyBinary()
{
   LABEL_t label = static_cast<LABEL_t>(ui->combo_label->currentIndex());
   //Process binary image
   Mat binary = temp.clone();
   img_calib_->getBinary(&binary,img_calib_->getLabelConfiguration(label));
   //Display Image
   image_ =  QImage( binary.data,
                     binary.cols, binary.rows,
                     static_cast<int>(binary.step),
                     QImage::Format_RGB888 ); 
   scene_->clear();
   scene_->addPixmap(QPixmap::fromImage(image_));
}

void MainWindow::interactWithUser()
{
   if(draw_mode){
      if(ui->check_draw->isChecked()){
      //Draw cross
         Mat draw = temp.clone();
         img_calib_->drawCenter(&draw);
         image_ =  QImage( draw.data,
                 draw.cols, draw.rows,
                 static_cast<int>(draw.step),
                 QImage::Format_RGB888 );
         scene_->clear();
         scene_->addPixmap(QPixmap::fromImage(image_));
      }  
   } 
}

// BUTTONS
void MainWindow::on_bt_grab_clicked()
{
   bool multiple_send_request = ui->radio_multiple->isChecked();
   msg_.is_multiple = multiple_send_request;
   msg_.frequency = ui->spin_framerate->value();
   msg_.type = (int)pow(2,ui->combo_aqtype->currentIndex());
   imgreq_pub_.publish(msg_);
}

void MainWindow::on_bt_stop_clicked()
{
   msg_.is_multiple = false;
   msg_.frequency = ui->spin_framerate->value();
   msg_.type = (int)pow(2,ui->combo_aqtype->currentIndex());
   imgreq_pub_.publish(msg_);
}

void MainWindow::on_bt_setdist_clicked()
{
   QStringList pixValues = ui->line_pixdist->text().split(",");
   vector<short unsigned int> values; values.clear();
   int targetValues = ui->spin_maxdist->value()/ui->spin_step->value();
   bool bad_configuration = false;
   int bad_conf_type = 0;
   if(targetValues!=pixValues.size()) { 
      bad_configuration = true; bad_conf_type = 1; 
   } else {
      for(unsigned int val=0;val<pixValues.size();val++)
         values.push_back(pixValues[val].toInt());
         
      unsigned int i = 0;
      while( ((i+1)<values.size()) && !bad_configuration){
         if(values[i]>values[i+1]){
            bad_configuration = true; bad_conf_type = 2;
         }
         i++;  
      }
   }
   
   if(bad_configuration){
      ROS_ERROR("Bad mirror configuration (%d)",bad_conf_type);
      QString err;
      if(bad_conf_type==1) err = "Wrong number of distance values.\n"
      +QString::number(targetValues)+" arguments needed but "
      +QString::number(pixValues.size())+" were provided";
      else err = "Wrong sequence of distance values.";
      QMessageBox::critical(NULL, QObject::tr("Bad Distances Configuration"),
         QObject::tr(err.toStdString().c_str()));
   }else {
      ROS_INFO("Correct mirror configuration sent!");
      mirrorConfig msg;
      msg.max_distance = ui->spin_maxdist->value();
      msg.step = ui->spin_step->value();
      msg.pixel_distances = values;
      mirror_pub_.publish(msg);
   }
}

void MainWindow::on_bt_setlut_clicked()
{
   vision_pub_.publish(img_calib_->getLutConfiguration());
   ROS_INFO("Correct vision configuration sent!");
}

void MainWindow::on_bt_setimg_clicked()
{
   image_pub_.publish(img_calib_->getImageConfiguration());
   ROS_INFO("Correct image configuration sent!");
}

void MainWindow::on_bt_screenshot_clicked()
{
   QString path = QString(getenv("HOME"))+QString("/catkin_ws/src/minho_team_tools/vision_calib/screenshots/");
   QString file = "Screenshot_";
   file.append(QString::number(std::time(0))); 
   file.append(".png");
   path+=file;
   ROS_INFO("Saved screenshot to %s",path.toStdString().c_str());
   imwrite(path.toStdString().c_str(),temp);
}

void MainWindow::on_check_draw_clicked(bool state)
{
   draw_mode = state;
}

//SLIDEBARS
void MainWindow::on_h_min_valueChanged(int value)
{
   ui->lb_hmin->setText(QString::number(value));
   img_calib_->updateCurrentConfiguration(static_cast<LABEL_t>(ui->combo_label->currentIndex())
                                          ,H
                                          ,MIN
                                          ,value);
   
}

void MainWindow::on_h_max_valueChanged(int value)
{
   ui->lb_hmax->setText(QString::number(value));
   img_calib_->updateCurrentConfiguration(static_cast<LABEL_t>(ui->combo_label->currentIndex())
                                          ,H
                                          ,MAX
                                          ,value);
}

void MainWindow::on_s_min_valueChanged(int value)
{
   ui->lb_smin->setText(QString::number(value));
   img_calib_->updateCurrentConfiguration(static_cast<LABEL_t>(ui->combo_label->currentIndex())
                                          ,S
                                          ,MIN
                                          ,value);
   
}

void MainWindow::on_s_max_valueChanged(int value)
{
   ui->lb_smax->setText(QString::number(value));
   img_calib_->updateCurrentConfiguration(static_cast<LABEL_t>(ui->combo_label->currentIndex())
                                          ,S
                                          ,MAX
                                          ,value);
}

void MainWindow::on_v_min_valueChanged(int value)
{
   ui->lb_vmin->setText(QString::number(value));
   img_calib_->updateCurrentConfiguration(static_cast<LABEL_t>(ui->combo_label->currentIndex())
                                          ,V
                                          ,MIN
                                          ,value);
}

void MainWindow::on_v_max_valueChanged(int value)
{
   ui->lb_vmax->setText(QString::number(value));
   img_calib_->updateCurrentConfiguration(static_cast<LABEL_t>(ui->combo_label->currentIndex())
                                          ,V
                                          ,MAX
                                          ,value);
}
//SPINBOXES
void MainWindow::on_spin_tilt_valueChanged(int value)
{
   imageConfig msg;
   msg.center_x = ui->spin_cx->value();
   msg.center_y = ui->spin_cy->value();
   msg.tilt = value;
   img_calib_->imageConfigFromMsg(msg);
}
void MainWindow::on_spin_cx_valueChanged(int value)
{
   imageConfig msg;
   msg.center_x = value;
   msg.center_y = ui->spin_cy->value();
   msg.tilt = ui->spin_tilt->value();
   img_calib_->imageConfigFromMsg(msg);
}
void MainWindow::on_spin_cy_valueChanged(int value)
{
   imageConfig msg;
   msg.center_x = ui->spin_cx->value();
   msg.center_y = value;
   msg.tilt = ui->spin_tilt->value();
   img_calib_->imageConfigFromMsg(msg);
}
   
//COMBOBOXES
void MainWindow::on_combo_label_currentIndexChanged(int index)
{
   LABEL_t label = static_cast<LABEL_t>(index);
   loadValuesOnTrackbars(img_calib_->getLabelConfiguration(label));
   
}
void MainWindow::on_combo_aqtype_currentIndexChanged(int index)
{
   on_bt_grab_clicked();   
}
void MainWindow::loadValuesOnTrackbars(minho_team_ros::label labelconf)
{
   ui->h_min->setValue(labelconf.H.min);
   ui->h_max->setValue(labelconf.H.max);
   ui->s_min->setValue(labelconf.S.min);
   ui->s_max->setValue(labelconf.S.max);
   ui->v_min->setValue(labelconf.V.min);
   ui->v_max->setValue(labelconf.V.max);
}

void MainWindow::loadMirrorValues(minho_team_ros::mirrorConfig mirrorConf)
{
   //mirrorConfig
   ui->spin_step->setValue(mirrorConf.step);
   ui->spin_maxdist->setValue(mirrorConf.max_distance);
   QString distances = "";
   for(unsigned int i=0;i<mirrorConf.pixel_distances.size();i++)
      distances+=QString::number(mirrorConf.pixel_distances[i])
               + QString(",");
   distances.remove(distances.size()-1,1);
   ui->line_pixdist->setText(distances);
}

void MainWindow::loadImageValues(minho_team_ros::imageConfig imageConf)
{
   //mirrorConfig
   ui->spin_tilt->setValue(imageConf.tilt);
   ui->spin_cx->setValue(imageConf.center_x);
   ui->spin_cy->setValue(imageConf.center_y);
}

