/*
 * 25/11/2015 : kostasl Testing OpenCV bg substraction - to process larva in vial recording timelapses.
 * App uses BG substyraction MOG2, with a slow learning rate.
 * then Uses Open and Close / Dilation contraction techniques to get rid of noise and fill gaps
 * //Next is to detect/Count Larva on screen use some Form of Particle Filter, concentration filter.
 * User:
 * Chooses input video file, then on the second dialogue choose the text file to export track info in CSV format.
 * The green box defines the region over which the larvae are counted-tracked and recorded to file.
 * Once the video begins to show, use to left mouse clicks to define a new region in the image over which you want to count the larvae.
 * Press p to pause Image. once paused:
 *  s to save snapshot.
 *  2 Left Clicks to define the 2 points of region-of interest for tracking.
 *  m to show the masked image of the larva against BG.
 *  t Start Tracking
 *  q Exit Quit application
 *
 * NOTE: Changing ROI hits SEG. FAULTs in update tracks of the library. So I made setting of ROI only once.
 * The Area is locked after t is pressed to start tracking. Still it fails even if I do it through cropping the images.
 * So I reverted to not tracking - as the code does not work well - I am recording blobs For now
 *
 *  Dependencies : opencv3
 */
#include <iostream>
#include <sstream>

#include <QString>
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QDir>
#include <QFileDialog>
#include <QTextStream>

#include <opencv2/highgui/highgui.hpp>
#include <opencv2/core/core.hpp>
#include "opencv2/imgproc/imgproc.hpp"
#include <opencv2/video/video.hpp>
#include "opencv2/video/background_segm.hpp"


#include <cvBlob/cvblob.h>

cv::Mat frame; //current frame
cv::Mat fgMaskMOG; //fg mask fg mask generated by MOG2 method
cv::Mat fgMaskMOG2; //fg mask fg mask generated by MOG2 method
cv::Mat fgMaskGMG; //fg mask fg mask generated by GMG method
cv::Ptr<cv::BackgroundSubtractor> pMOG; //MOG Background subtractor
cv::Ptr<cv::BackgroundSubtractorMOG2> pMOG2; //MOG2 Background subtractor
cv::Ptr<cv::BackgroundSubtractor> pGMG; //GMG Background subtractor

//Global Shortcut of Type conversion to legacy IplImage
IplImage  *labelImg;
IplImage frameImg;

//Region of interest
cv::Rect roi(cv::Point(0,0),cv::Point(1024,768));
cv::Point ptROI1;
cv::Point ptROI2;

int keyboard; //input from keyboard
int screenx,screeny;
bool showMask; //True will show the BGSubstracted IMage/Processed Mask
bool bROIChanged;
bool bPaused;
bool bTracking;
bool b1stPointSet;

using namespace std;

void processVideo(QString videoFilename,QString outFileCSV);
void checkPauseRun(int& keyboard,string frameNumberString);
bool saveImage(string frameNumberString,cv::Mat& img);
int countObjectsviaContours(cv::Mat& srcimg );
int countObjectsviaBlobs(cv::Mat& srcimg,cvb::CvBlobs& blobs,cvb::CvTracks& tracks);
int saveTrackedBlobs(cvb::CvBlobs& blobs,QString filename,string frameNumber);
int saveTracks(cvb::CvTracks& tracks,QString filename,std::string frameNumber);
int saveTrackedBlobsTotals(cvb::CvBlobs& blobs,cvb::CvTracks& tracks,QString filename,std::string frameNumber);


void CallBackFunc(int event, int x, int y, int flags, void* userdata); //Mouse Callback

int main(int argc, char *argv[])
{
    bROIChanged = false;
    bPaused = true;
    showMask = false;
    bTracking = false;

    QApplication app(argc, argv);
    QQmlApplicationEngine engine;

    QString invideoname = QFileDialog::getOpenFileName(0, "Select timelapse video to Process", qApp->applicationDirPath(), "Video file (*.mpg *.avi)", 0, 0); // getting the filename (full path)
    QString outfilename = invideoname;
    outfilename.truncate(outfilename.lastIndexOf("."));
    outfilename = QFileDialog::getSaveFileName(0, "Save tracks to output", outfilename + "_pos.csv", "CSV files (*.csv);", 0, 0); // getting the filename (full path)

    //engine.load(QUrl(QStringLiteral("qrc:///main.qml")));

    // get the applications dir path and expose it to QML

       //create GUI windows
       string strwinName = "VialFrame";
       cv::namedWindow(strwinName,CV_WINDOW_AUTOSIZE);
       //set the callback function for any mouse event
       cv::setMouseCallback(strwinName, CallBackFunc, NULL);

       //create Background Subtractor objects
              //(int history=500, double varThreshold=16, bool detectShadows=true
       pMOG2 =  cv::createBackgroundSubtractorMOG2(30,16,false); //MOG2 approach
       //(int history=200, int nmixtures=5, double backgroundRatio=0.7, double noiseSigma=0)
       //pMOG =  new cv::BackgroundSubtractorMOG(30,12,0.7,0.0); //MOG approach
       //pGMG =  new cv::BackgroundSubtractorGMG(); //GMG approach

       //unsigned int hWnd = cvGetWindowHandle("VialFrame");
       //cv::displayOverlay(strwinName,"Tracking: " + outfilename.toStdString(), 2000 );
       //"//home/kostasl/workspace/QtTestOpenCV/pics/20151124-timelapse.mpg"
       processVideo(invideoname,outfilename);


       //destroy GUI windows
       cv::destroyAllWindows();

       cv::waitKey(0);                                          // Wait for a keystroke in the window


       //pMOG->~BackgroundSubtractor();
       pMOG2->~BackgroundSubtractor();
       //pGMG->~BackgroundSubtractor();

       //
       //return ;

        app.quit();
       return EXIT_SUCCESS;

}


/*
 * Process Larva timelapse, removing BG, detecting moving larva- Setting the learning rate will change the time required
 * to remove a pupa from the scene -
 */

void processVideo(QString videoFilename,QString outFileCSV) {
    unsigned int nLarva=  0;
    //Speed that stationary objects are removed
    double dLearningRate = 0.1;

    //Font for Tracking
    CvFont trackFnt;
    cvInitFont(&trackFnt, CV_FONT_HERSHEY_DUPLEX, 0.4, 0.4, 0, 1);

    //Make Variation of FileNames for other Output
    QString trkoutFileCSV = outFileCSV;
    trkoutFileCSV.truncate(trkoutFileCSV.lastIndexOf("."));
    trkoutFileCSV.append("_tracks.csv");
    QString vialcountFileCSV = outFileCSV;
    vialcountFileCSV.truncate(vialcountFileCSV.lastIndexOf("."));
    vialcountFileCSV.append("_N.csv");
    //REPORT
    cout << "Tracking data saved to :" << vialcountFileCSV.toStdString()  << endl;
    cout << "\t " << outFileCSV.toStdString() << endl;
    cout << "\t " << trkoutFileCSV.toStdString()  << endl;


    //For Morphological Filter
    //cv::Size sz = cv::Size(3,3);
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_CROSS,cv::Size(3,3),cv::Point(-1,-1));
    cv::Mat kernelClose = cv::getStructuringElement(cv::MORPH_CROSS,cv::Size(2,2),cv::Point(-1,-1));

    //Structures to hold blobs & Tracks
    cvb::CvBlobs blobs;
    cvb::CvTracks tracks;

    //create the capture object
    cv::VideoCapture capture(videoFilename.toStdString());
    if(!capture.isOpened()){
        //error in opening the video input
        std::cerr << "Unable to open video file: " << videoFilename.toStdString() << std::endl;
        std::exit(EXIT_FAILURE);
    }

    unsigned int nFrame = 0;

    //read input data. ESC or 'q' for quitting
    while( (char)keyboard != 'q' && (char)keyboard != 27 ){
        //read the current frame
        if(!capture.read(frame)) {
            if (nFrame ==0)
            {
                std::cerr << "Unable to read first frame." << std::endl;
                exit(EXIT_FAILURE);
            }
            else
            {
                std::cerr << "Unable to read next frame." << std::endl;
                cout << nFrame << " frames of Video processing done! " << endl;
                break;
           }
        }
        nFrame = capture.get(CV_CAP_PROP_POS_FRAMES);

        if (nFrame > 200) //Slow Down Learning After Initial Exposure
            dLearningRate = 0.00020;

        //update the background model
        pMOG2->apply(frame, fgMaskMOG2,dLearningRate);
        //pMOG->operator()(frame, fgMaskMOG);
        //pGMG->operator ()(frame,fgMaskGMG);
        //get the frame number and write it on the current frame
         //cv::imshow("FG Mask MOG 2 Before MoRPH", fgMaskMOG2);

        //Apply Open Operation
         cv::morphologyEx(fgMaskMOG2,fgMaskMOG2, cv::MORPH_OPEN, kernel,cv::Point(-1,-1),1);
         cv::morphologyEx(fgMaskMOG2,fgMaskMOG2, cv::MORPH_CLOSE, kernel,cv::Point(-1,-1),2);
         //cv::dilate(fgMaskMOG2,fgMaskMOG2,kernelClose, cv::Point(-1,-1),1);
         //Needs extra erode to get rid to food marks
         cv::erode(fgMaskMOG2,fgMaskMOG2,kernelClose, cv::Point(-1,-1),2);


        //Put Info TextOn Frame
        //Frame Number
        std::stringstream ss;
        cv::rectangle(frame, cv::Point(10, 2), cv::Point(100,20),
                  cv::Scalar(255,255,255), -1);
        ss << nFrame;
        std::string frameNumberString = ss.str();
        cv::putText(frame, frameNumberString.c_str(), cv::Point(15, 15),
                cv::FONT_HERSHEY_SIMPLEX, 0.5 , cv::Scalar(0,0,0));

        //Count on Original Frame
        std::stringstream strCount;
        strCount << "N:" << (nLarva);
        cv::rectangle(frame, cv::Point(10, 25), cv::Point(100,45), cv::Scalar(255,255,255), -1);
        cv::putText(frame, strCount.str(), cv::Point(15, 35),
                cv::FONT_HERSHEY_SIMPLEX, 0.5 , cv::Scalar(0,0,0));
        //DRAW ROI RECT
        cv::rectangle(frame,roi,cv::Scalar(50,250,50));

        //cvb::CvBlobs blobs;
        if (bTracking)
        {
            //Simple Solution was to Use Contours To measure Larvae
            //countObjectsviaContours(fgMaskMOG2); //But not as efficient

           // cvb::CvBlobs blobs;
           nLarva = countObjectsviaBlobs(fgMaskMOG2, blobs,tracks);
           saveTrackedBlobs(blobs,outFileCSV,frameNumberString);
           saveTrackedBlobsTotals(blobs,tracks,vialcountFileCSV,frameNumberString);

            //ROI with TRACKs Fails
            const int inactiveFrameCount = 10; //Number of frames inactive until track is deleted
            const int thActive = 2;// If a track becomes inactive but it has been active less than thActive frames, the track will be deleted.

            //Tracking has Bugs when it involves Setting A ROI. SEG-FAULTS
            cvb::cvUpdateTracks(blobs,tracks, 10, inactiveFrameCount,thActive);
            saveTracks(tracks,trkoutFileCSV,frameNumberString);
            cvb::cvRenderTracks(tracks, &frameImg, &frameImg,CV_TRACK_RENDER_ID,&trackFnt);
        }


        //show the current frame and the fg masks
        cv::imshow("VialFrame", frame);

        if (showMask)
        {
            cv::imshow("FG Mask MOG 2 after Morph", fgMaskMOG2);
            //cv::imshow("FG Mask GMG ", fgMaskGMG);
        }

        //get the input from the keyboard
        keyboard = cv::waitKey( 30 );

        checkPauseRun(keyboard,frameNumberString);


    } //main While loop
    //delete capture object
    capture.release();

    cvb::cvReleaseTracks(tracks);
    cvb::cvReleaseBlobs(blobs);
    std::cout << "Exiting video processing loop." << endl;
}

void checkPauseRun(int& keyboard,string frameNumberString)
{
    //implemend Pause
    if ((char)keyboard == 'p')
            bPaused = true;

    if ((char)keyboard == 't') //Toggle Tracking
        bTracking = !bTracking;

        while (bPaused)
        {
            int ms = 20;
            struct timespec ts = { ms / 1000, (ms % 1000) * 1000 * 1000 };
            nanosleep(&ts, NULL);
            //Wait Until Key to unpause is pressed
            keyboard = cv::waitKey( 30 );

            if ((char)keyboard == 's')
            {
               saveImage(frameNumberString,frame);
            }

            if ((char)keyboard == 'r')
                bPaused = false;

            //Toggle Show the masked - where blob id really happens
            if ((char)keyboard == 'm')
                 showMask = !showMask;

            if ((char)keyboard == 'q')
                 break; //Main Loop Will handle this


            //if ((char)keyboard == 'c')
            cv::imshow("VialFrame", frame);

        }

    //Toggle Show the masked - where blob id really happens
    if ((char)keyboard == 'm')
             showMask = !showMask;
}

bool saveImage(string frameNumberString,cv::Mat& img)
{

    //Save Output BG Masks
    QString imageToSave =   QString::fromStdString( "output_MOG_" + frameNumberString + ".png");
    QString dirToSave = qApp->applicationDirPath();

    dirToSave.append("/pics/");
    imageToSave.prepend(dirToSave);

    if (!QDir(dirToSave).exists())
    {
        cerr << "Make directory " << dirToSave.toStdString() << std::endl;
        QDir().mkpath(dirToSave);
    }

    bool saved = cv::imwrite(imageToSave.toStdString(), img);
    if(!saved) {
        cv::putText(img,"Failed to Save " + imageToSave.toStdString(), cv::Point(25, 25), cv::FONT_HERSHEY_SIMPLEX, 0.5 , cv::Scalar(250,250,250));
        cv::putText(img,"Failed to Save" + imageToSave.toStdString(), cv::Point(25, 25), cv::FONT_HERSHEY_SIMPLEX, 0.4 , cv::Scalar(0,0,0));
        cerr << "Unable to save " << imageToSave.toStdString() << endl;
        return false;
    }
    else
    {
     cout << "Saved image " << imageToSave.toStdString() <<endl;
    }

    cv::imshow("Saved Frame", img);

    return true;
}

int countObjectsviaContours(cv::Mat& srcimg )
{
     cv::Mat imgTraced;
     srcimg.copyTo(imgTraced);
     vector< vector <cv::Point> > contours; // Vector for storing contour
     vector< cv::Vec4i > hierarchy;

     cv::findContours( imgTraced, contours, hierarchy,CV_RETR_EXTERNAL, CV_CHAIN_APPROX_SIMPLE ); // Find the contours in the image
     for( unsigned int i = 0; i< contours.size(); i=hierarchy[i][0] ) // iterate through each contour.
        {
          cv::Rect r= cv::boundingRect(contours[i]);
          cv::rectangle(imgTraced,r, cv::Scalar(255,0,0),1,8,0);
          cv::rectangle(frame,r, cv::Scalar(255,0,0),1,8,0);
        }

     //Write text For Count on Original Frame
     std::stringstream strCount;
     strCount << "N:" << ((int)contours.size());

     cv::rectangle(frame, cv::Point(540, 2), cv::Point(690,20), cv::Scalar(255,255,255), -1);
     cv::putText(frame, strCount.str(), cv::Point(545, 15),
             cv::FONT_HERSHEY_SIMPLEX, 0.5 , cv::Scalar(0,0,0));

    std::cout << " Larvae  "<< strCount.str() << endl;
    //imshow("Contoured Image",frame);


    // To get rid of the smaller object and the outer rectangle created
      //because of the additional mask image we enforce a lower limit on area
      //to remove noise and an upper limit to remove the outer border.

 /* if (contourArea(contours_poly[i])>(mask.rows*mask.cols/10000) && contourArea(contours_poly[i])<mask.rows*mask.cols*0.9){
      boundRect[i] = boundingRect( Mat(contours_poly[i]) );
      minEnclosingCircle( (Mat)contours_poly[i], center[i], radius[i] );
      circle(drawing,center[i], (int)radius[i], Scalar(255,255,255), 2, 8, 0);
      rectangle(drawing,boundRect[i], Scalar(255,255,255),2,8,0);
      num_object++;
}
      */

}


int countObjectsviaBlobs(cv::Mat& srcimg,cvb::CvBlobs& blobs,cvb::CvTracks& tracks)
{

    ///// Finding the blobs ////////

     IplImage fgMaskImg;
///  REGION OF INTEREST - UPDATE - SET
     frameImg =  frame; //Convert The Global frame to lplImage
     fgMaskImg =  srcimg;

    if (bROIChanged || ptROI2.x != 0)
    {
        cv::circle(frame,ptROI1,3,cv::Scalar(255,0,0),1);
        cv::circle(frame,ptROI2,3,cv::Scalar(255,0,0),1);

        //Set fLAG sO FROM now on Region of interest is used and cannot be changed.
        bROIChanged = true;
    }
    else
    {
        frameImg =  frame; //Convert The Global frame to lplImage
        fgMaskImg =  srcimg;
    }

    cvSetImageROI(&frameImg, roi);
    cvSetImageROI(&fgMaskImg, roi);

    IplImage  *labelImg=cvCreateImage(cvGetSize(&frameImg), IPL_DEPTH_LABEL, 1);

    unsigned int result = cvb::cvLabel( &fgMaskImg, labelImg, blobs );

    //Filtering the blobs
    cvb::cvFilterByArea(blobs,10,100);

    // render blobs in original image
    cvb::cvRenderBlobs( labelImg, blobs, &fgMaskImg, &frameImg,CV_BLOB_RENDER_CENTROID|CV_BLOB_RENDER_BOUNDING_BOX | CV_BLOB_RENDER_COLOR);

    //cout << strCount.str() << endl;
    // *always* remember freeing unused IplImages
    cvReleaseImage( &labelImg );


    return (int)blobs.size();

}

int saveTrackedBlobs(cvb::CvBlobs& blobs,QString filename,std::string frameNumber)
{
    int cnt = 0;
    bool bNewFileFlag = true;
    QFile data(filename);
    if (data.exists())
        bNewFileFlag = false;

    if(data.open(QFile::WriteOnly |QFile::Append))
    {
        QTextStream output(&data);
        if (bNewFileFlag)
             output << "frameN,SerialN,BlobLabel,Centroid_X,Centroid_Y,Area" << endl;


        for (cvb::CvBlobs::const_iterator it=blobs.begin(); it!=blobs.end(); ++it)
        {
            cnt++;
            cvb::CvBlob cvB = *it->second;
            cvb::CvLabel cvL = it->first;

            //Printing the position information
            output << frameNumber.c_str() << "," << cnt <<","<< cvB.label << "," << cvB.centroid.x <<","<< cvB.centroid.y  <<","<< cvB.area  <<  endl;
          }
        }
    data.close();

    return cnt;
}

//Saves the total Number of Counted Blobs and Tracks only
int saveTrackedBlobsTotals(cvb::CvBlobs& blobs,cvb::CvTracks& tracks,QString filename,std::string frameNumber)
{

    bool bNewFileFlag = true;
    QFile data(filename);
    if (data.exists())
        bNewFileFlag = false;
    if(data.open(QFile::WriteOnly |QFile::Append))
    {

        QTextStream output(&data);
        if (bNewFileFlag)
             output << "frameN,blobN,TracksN" << endl;

        output << frameNumber.c_str() << "," << blobs.size() << "," << tracks.size() << endl;
    }
    data.close();

    return 1;
}





int saveTracks(cvb::CvTracks& tracks,QString filename,std::string frameNumber)
{
    bool bNewFileFlag = true;
    QFile data(filename);
    if (data.exists())
        bNewFileFlag = false;
    int cnt = 0;

    if(data.open(QFile::WriteOnly |QFile::Append))
    {

        QTextStream output(&data);
        if (bNewFileFlag)
             output << "frameN,TrackID,TrackBlobLabel,Centroid_X,Centroid_Y,Lifetime,Active,Inactive" << endl;


        for (cvb::CvTracks::const_iterator it=tracks.begin(); it!=tracks.end(); ++it)
        {
            cnt++;
            cvb::CvTrack cvT = *it->second;
            //cvb::CvLabel cvL = it->first;

            //Printing the position information +
            //+ lifetime; ///< Indicates how much frames the object has been in scene.
            //+active; ///< Indicates number of frames that has been active from last inactive period.
            //+ inactive; ///< Indicates number of frames that has been missing.
            output << frameNumber.c_str()  << "," << cvT.id  << "," << cvT.label  << "," << cvT.centroid.x << "," << cvT.centroid.y << "," << cvT.lifetime  << "," << cvT.active  << "," << cvT.inactive <<  endl;
          }
        }
    data.close();

     return cnt;
}
//Mouse Call Back Function
void CallBackFunc(int event, int x, int y, int flags, void* userdata)
{
     if  ( event == cv::EVENT_LBUTTONDOWN )
     {

         //ROI is locked once tracking begins
        if (bPaused && !bROIChanged) //CHANGE ROI Only when Paused and ONCE
        { //Change 1st Point if not set or If 2nd one has been set
             if ( b1stPointSet == false)
             {
                ptROI1.x = x;
                ptROI1.y = y;

                cv::circle(frame,ptROI1,3,cv::Scalar(255,0,0),1);
                b1stPointSet = true;

             }
             else //Second & Final Point
             {
                ptROI2.x = x;
                ptROI2.y = y;
                cv::Rect newROI(ptROI1,ptROI2);
                roi = newROI;
                //Draw the 2 points
                cv::circle(frame,ptROI2,3,cv::Scalar(255,0,0),1);
                cv::rectangle(frame,newROI,cv::Scalar(0,0,250));

                b1stPointSet = false; //Rotate To 1st Point Again
             }
        }


        cout << "Left button of the mouse is clicked - position (" << x << ", " << y << ")" << endl;
     }
     else if  ( event == cv::EVENT_RBUTTONDOWN )
     {

         //cout << "Right button of the mouse is clicked - position (" << x << ", " << y << ")" << endl;
     }
     else if  ( event == cv::EVENT_MBUTTONDOWN )
     {
          cout << "Middle button of the mouse is clicked - position (" << x << ", " << y << ")" << endl;
     }
     else if ( event == cv::EVENT_MOUSEMOVE )
     {
         // cout << "Mouse move over the window - position (" << x << ", " << y << ")" << endl;

     }
}
