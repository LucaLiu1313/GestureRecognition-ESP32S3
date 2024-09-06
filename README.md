# GestureRecognition-ESP32S3
the mian construction are as follows
```
project
 mobile10.onnx                  		# model in the form of onnx
 onnx_pred_s.py                  	# predict the hand gesture in a given picture
├── main                  		# project to connect WIFI, on ESP32S3
│   ├── CMakeList         	
│   ├── station_upload.c           	# the file used to build web server,connect WIFI & uoload pics captured by esp32s3 camera
├── Makefile    
├── CMakeList.txt                  		
├── sdkconfig                       #espressif info
```
the project is finished
Now you can use esp32s3-cam to monitor the video stream,and then capture a frame,by sending the photo to the model,you can obtain the predicted result of your gesture.
