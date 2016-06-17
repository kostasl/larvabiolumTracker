///*
//// 25/11/2015 : kostasl Testing OpenCV bg substraction - to process larva in vial recording timelapses.
 //// App uses BG substyraction MOG2, with a slow learning rate.
 //// then Uses Open and Close / Dilation contraction techniques to get rid of noise and fill gaps
 //// Then uses cvBlob library to track and count larva on BG-processed images.
 //// The lib sources have been included to the source and slightly modified in update tracks to fix a bug.
 ////
 ///* User:
 ///* Chooses input video file, then on the second dialogue choose the text file to export track info in CSV format.
 ///* The green box defines the region over which the larvae are counted-tracked and recorded to file.
 ///* Once the video begins to show, use to left mouse clicks to define a new region in the image over which you want to count the larvae.
 ///* Press p to pause Image. once paused:
 ///*  s to save snapshots in CSV outdir pics subfolder.
 ///*  2 Left Clicks to define the 2 points of region-of interest for tracking.
 ///*  m to show the masked image of the larva against BG.
 ///*  t Start Tracking
 ///*  q Exit Quit application
 ///*
 ///* NOTE: Changing ROI hits SEG. FAULTs in update tracks of the library. So I made setting of ROI only once.
 ///* The Area is locked after t is pressed to start tracking. Still it fails even if I do it through cropping the images.
 ///* So I reverted to not tracking - as the code does not work well - I am recording blobs For now
 ///*
 ///*  Dependencies : opencv3
 ///*
 /// TODO: Detect stopped Larva - either pupating or stuck
 ////////


#include <larvatrack.h>

//Global Variables
QElapsedTimer gTimer;

QString gstroutDirCSV; //The Output Directory

cv::Mat frame; //current frame
cv::Mat frameCpy; //copy of Current frame;
cv::Mat fgMaskMOG; //fg mask fg mask generated by MOG2 method
cv::Mat fgMaskMOG2; //fg mask fg mask generated by MOG2 method
cv::Mat fgMaskGMG; //fg mask fg mask generated by GMG method
cv::Ptr<cv::BackgroundSubtractor> pMOG; //MOG Background subtractor
cv::Ptr<cv::BackgroundSubtractorMOG2> pMOG2; //MOG2 Background subtractor
cv::Ptr<cv::BackgroundSubtractor> pGMG; //GMG Background subtractor

//Global Shortcut of Type conversion to legacy IplImage
IplImage  *labelImg;
IplImage frameImg;


ltROI roi(cv::Point(0,0),cv::Point(1024,768));
ltROIlist vRoi;
cv::Point ptROI1;
cv::Point ptROI2;

//Structures to hold blobs & Tracks
cvb::CvBlobs blobs;
cvb::CvTracks tracks;

//Font for Reporting - Tracking
CvFont trackFnt;


int keyboard; //input from keyboard
int screenx,screeny;
bool showMask; //True will show the BGSubstracted IMage/Processed Mask
bool bROIChanged;
bool bPaused;
bool bTracking;
bool bSaveImages = false;
bool b1stPointSet;
bool bMouseLButtonDown;

//Bioluminescence Record
QString infilename;
std::vector<unsigned int> vLumRec; //Pointer to array of biolunim Data
double gdLumRecfps = 0.5;
double gdvidfps = 20.0;
unsigned int gmaxLumValue = 0;

//Area Filters
double dMeanBlobArea = 10;
double dVarBlobArea = 50;

#define LOW_LOWERBOUND_BLOB_AREA 60.0
#define LOW_UPPERBOUND_BLOB_AREA 1200.0

//BG History
const int MOGhistory        = 200;
//Processing Loop delay
uint cFrameDelayms    = 1;
double dLearningRate        = 0.001;

using namespace std;



int main(int argc, char *argv[])
{
    bROIChanged = false;
    bPaused = true;
    showMask = false;
    bTracking = false;

    QApplication app(argc, argv);
    QQmlApplicationEngine engine;


    //outfilename.truncate(outfilename.lastIndexOf("."));
    QString outfilename = QFileDialog::getSaveFileName(0, "Save tracks to output","VX_pos.csv", "CSV files (*.csv);", 0, 0); // getting the filename (full path)

    //outfilename.truncate(outfilename.lastIndexOf("."));
    infilename = QFileDialog::getOpenFileName(0, "Select Biolum. record","/home/klagogia/Dropbox/Bioluminesce/biolum/dat/mb247xga", "CSV files (*.csv);; TXT files (*.txt);;", 0, 0); // getting the filename (full path)

    readBiolumFile(vLumRec, infilename, gmaxLumValue);
    // get the applications dir pah and expose it to QML
    //engine.load(QUrl(QStringLiteral("qrc:///main.qml")));
    //Init Font
    cvInitFont(&trackFnt, CV_FONT_HERSHEY_DUPLEX, 0.4, 0.4, 0, 1);

    gTimer.start();
    //create GUI windows
    string strwinName = "VialFrame";
    cv::namedWindow(strwinName,CV_WINDOW_NORMAL | CV_WINDOW_KEEPRATIO);
    //set the callback function for any mouse event
    cv::setMouseCallback(strwinName, CallBackFunc, NULL);


    //Initialize The Track and blob vectors
    cvb::cvReleaseTracks(tracks);
    cvb::cvReleaseBlobs(blobs);


    //create Background Subtractor objects
    //(int history=500, double varThreshold=16, bool detectShadows=true
    //OPENCV 2
    //pMOG2 =  cv::createBackgroundSubtractorMOG2(MOGhistory,16,false); //MOG2 approach
    //OPENCV 3
    // BENA!!! *************************
    pMOG2 = cv::createBackgroundSubtractorMOG2();
    //pMOG2 = new cv::BackgroundSubtractorMOG2(MOGhistory,16,false); //MOG2 approach

    // ************************************* END BENA

    //(int history=200, int nmixtures=5, double backgroundRatio=0.7, double noiseSigma=0)
    //pMOG =  new cv::BackgroundSubtractorMOG(30,12,0.7,0.0); //MOG approach
    //pGMG =  new cv::BackgroundSubtractorGMG(); //GMG approach

    //unsigned int hWnd = cvGetWindowHandle("VialFrame");
    try{ //If cv is compiled with QT support //Remove otherwise
        //cv::setWindowTitle(strwinName, outfilename.toStdString());
        //cv::displayOverlay(strwinName,"Tracking: " + outfilename.toStdString(), 20000 );
    }catch(int e)
    {
        cerr << "OpenCV not compiled with QT support! can display overlay" << endl;
    }

    QString invideoname = "*.mpg";
    unsigned int istartFrame = 0;
    QStringList invideonames =QFileDialog::getOpenFileNames(0, "Select timelapse video to Process", qApp->applicationDirPath(), "Video file (*.mpg *.avi *.mp4 *.h264 *.mov)", 0, 0);

    //Show Video list to process
    cout << "Video List To process:" << endl;
    for (int i = 0; i<invideonames.size(); ++i)
    {
        invideoname = invideonames.at(i);
        cout << "*" <<  invideoname.toStdString() << endl;
    }
    //Go through Each Video - Hold Last Frame N , make it the start of the next vid.
    for (int i = 0; i<invideonames.size(); ++i)
    {
//        invideoname= QFileDialog::getOpenFileName(0, "Select timelapse video to Process", qApp->applicationDirPath(), "Video file (*.mpg *.avi)", 0, 0); // getting the filename (full path)
        invideoname = invideonames.at(i);
        cout << " Now Processing : "<< invideoname.toStdString() << endl;
        istartFrame = processVideo(invideoname,outfilename,istartFrame);

        if (istartFrame == 0)
        {
            cerr << "Could not load last video - Exiting loop." <<  endl;
            break;
        }
    }

    //destroy GUI windows
    cv::destroyAllWindows();
    cv::waitKey(0);                                          // Wait for a keystroke in the window

    //pMOG->~BackgroundSubtractor();
    pMOG2->~BackgroundSubtractor();
    //pGMG->~BackgroundSubtractor();

    //Empty The Track and blob vectors
    cvb::cvReleaseTracks(tracks);
    cvb::cvReleaseBlobs(blobs);

    //
    //return ;

    cout << "Total processing time : mins " << gTimer.elapsed()/60000.0 << endl;
    app.quit();
    return EXIT_SUCCESS;

}


/*
 * Process Larva timelapse, removing BG, detecting moving larva- Setting the learning rate will change the time required
 * to remove a pupa from the scene -
 */

unsigned int processVideo(QString videoFilename,QString outFileCSV,unsigned int startFrameCount) {
    unsigned int nLarva         =  0;
    //Speed that stationary objects are removed

    double dblRatioPxChanged    = 0.0;
    unsigned int nFrame = startFrameCount; //Current Frame Number


    //Make Variation of FileNames for other Output
    gstroutDirCSV = outFileCSV.left(outFileCSV.lastIndexOf("/"));
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
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE,cv::Size(1,2),cv::Point(-1,-1));
    cv::Mat kernelClose = cv::getStructuringElement(cv::MORPH_ELLIPSE,cv::Size(3,4),cv::Point(-1,-1));


    //create the capture object
    cv::VideoCapture capture(videoFilename.toStdString());
    gdvidfps = capture.get(CV_CAP_PROP_FPS);

    if(!capture.isOpened()){
        //error in opening the video input
        std::cerr << "Unable to open video file: " << videoFilename.toStdString() << std::endl;
        std::exit(EXIT_FAILURE);
    }


    //read input data. ESC or 'q' for quitting
    while( (char)keyboard != 'q' && (char)keyboard != 27 ){
        //read the current frame
        if(!capture.read(frame)) {
            if (nFrame == startFrameCount)
            {
                std::cerr << "Unable to read first frame." << std::endl;
                nFrame = 0; //Signals To caller that video could not be loaded.
                exit(EXIT_FAILURE);
            }
            else
            {
                std::cerr << "Unable to read next frame. So this video Is done." << std::endl;
                cout << nFrame << " frames of Video processed. Move on to next timelapse video? " << endl;
                break;
           }
        }
        //Add frames from Last video
        nFrame = capture.get(CV_CAP_PROP_POS_FRAMES) + startFrameCount;

        //If Mask shows that a large ratio of pixels is changing then - adjust learning rate to keep activity below 0.006

        if (nLarva > 1) //Added Count Limit (dblRatioPxChanged > 0.35 ||
            dLearningRate = max(min(dLearningRate*1.00002,0.001),0.00);
        else if (nLarva < 1 || dMeanBlobArea < 300)//(nFrame > MOGhistory*2)
            dLearningRate = dLearningRate*0.98; //Exp Reduction
        else
            dLearningRate = 0.000001; //Default Value -  Forgets


        //update the background model
        //OPEN CV 2.4
        pMOG2->apply(frame, fgMaskMOG2,dLearningRate);
        //OPENCV 3
//        pMOG->operator()(frame, fgMaskMOG2,dLearningRate);
        //get the frame number and write it on the current frame
        //erode to get rid to food marks
        cv::erode(fgMaskMOG2,fgMaskMOG2,kernel, cv::Point(-1,-1),1);
        //Do Close : erode(dilate())
        cv::morphologyEx(fgMaskMOG2,fgMaskMOG2, cv::MORPH_CLOSE, kernelClose,cv::Point(-1,-1),1);
        //cv::dilate(fgMaskMOG2,fgMaskMOG2,kernel, cv::Point(-1,-1),1);
        //Apply Open Operation dilate(erode())
        cv::morphologyEx(fgMaskMOG2,fgMaskMOG2, cv::MORPH_OPEN, kernel,cv::Point(-1,-1),1);


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
        cv::putText(frame, strCount.str(), cv::Point(15, 38),
                cv::FONT_HERSHEY_SIMPLEX, 0.5 , cv::Scalar(0,0,0));

        char buff[100];
        //Learning Rate
        //std::stringstream strLearningRate;
        std::sprintf(buff,"dL: %0.4f",dLearningRate);
        //strLearningRate << "dL:" << (double)(dLearningRate);
        cv::rectangle(frame, cv::Point(10, 50), cv::Point(100,70), cv::Scalar(255,255,255), -1);
        cv::putText(frame, buff, cv::Point(15, 63),
                cv::FONT_HERSHEY_SIMPLEX, 0.5 , cv::Scalar(0,0,0));

        //Time Rate - conv from ms to minutes

        std::sprintf(buff,"t: %0.2f",gTimer.elapsed()/(1000.0*60.0) );
        //strTimeElapsed << "" <<  << " m";
        cv::rectangle(frame, cv::Point(10, 75), cv::Point(100,95), cv::Scalar(255,255,255), -1);
        cv::putText(frame, buff, cv::Point(15, 88),
                cv::FONT_HERSHEY_SIMPLEX, 0.5 , cv::Scalar(0,0,0));

        //Count Fg Pixels // Ratio
        std::stringstream strFGPxRatio;
        dblRatioPxChanged = (double)cv::countNonZero(fgMaskMOG2)/(double)fgMaskMOG2.size().area();
        strFGPxRatio << "mA:" <<  dMeanBlobArea;
        cv::rectangle(frame, cv::Point(10, 100), cv::Point(100,120), cv::Scalar(255,255,255), -1);
        cv::putText(frame, strFGPxRatio.str(), cv::Point(15, 113),
                cv::FONT_HERSHEY_SIMPLEX, 0.5 , cv::Scalar(0,0,0));

        //Hold A copy of Frame With all txt
        frame.copyTo(frameCpy);
        //DRAW ROI RECT
        //cv::rectangle(frame,roi,cv::Scalar(50,250,50));
        drawROI();


        //cvb::CvBlobs blobs;
        if (bTracking)
        {
            //Simple Solution was to Use Contours To measure Larvae
            //countObjectsviaContours(fgMaskMOG2); //But not as efficient

           // cvb::CvBlobs blobs;
            nLarva = countObjectsviaBlobs(fgMaskMOG2, blobs,tracks,gstroutDirCSV,frameNumberString,dMeanBlobArea);

            //ROI with TRACKs Fails
            const int inactiveFrameCount = 10; //Number of frames inactive until track is deleted
            const int thActive = 2;// If a track becomes inactive but it has been active less than thActive frames, the track will be deleted.

            //Tracking has Bugs when it involves Setting A ROI. SEG-FAULTS
            //thDistance = 22 //Distance from Blob to track
            int thDistance = 20;
            cvb::cvUpdateTracks(blobs,tracks,vRoi, thDistance, inactiveFrameCount,thActive);
            saveTracks(tracks,trkoutFileCSV,frameNumberString);

            //cvb::cvRenderTracks(tracks, &frameImg, &frameImg,CV_TRACK_RENDER_ID,&trackFnt);
        }


        //show the current frame and the fg masks
        cv::imshow("VialFrame", frame);

        if (showMask)
        {
            cv::imshow("FG Mask MOG 2 after Morph", fgMaskMOG2);
            //cv::imshow("FG Mask GMG ", fgMaskGMG);
        }

       // if (!bTracking)
            //get the input from the keyboard
            keyboard = cv::waitKey( cFrameDelayms );


        checkPauseRun(keyboard,frameNumberString);


    } //main While loop
    //delete capture object
    capture.release();

    //delete kernel;
    //delete kernelClose;


    std::cout << "Exiting video processing loop." << endl;

    return nFrame;
}

void checkPauseRun(int& keyboard,string frameNumberString)
{
    //implemend Pause
    if ((char)keyboard == 'p')
    {
        //frame.copyTo(frameCpy);
        bPaused = true;
    }

    //Make Frame rate faster
    if ((char)keyboard == '+')
        cFrameDelayms--;
    //Slower
    if ((char)keyboard == '-')
        cFrameDelayms++;




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
                bSaveImages = !bSaveImages;

                saveImage(frameNumberString,gstroutDirCSV,frame);
            }

            if ((char)keyboard == 'r')
            {
                bPaused = false;
                gTimer.start();
            }

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

bool saveImage(string frameNumberString,QString dirToSave,cv::Mat& img)
{

    //Save Output BG Masks
    QString imageToSave =   QString::fromStdString( "output_MOG_" + frameNumberString + ".png");
    //QString dirToSave = qApp->applicationDirPath();

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

    //cv::imshow("Saved Frame", img);

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

    return contours.size();
}


int countObjectsviaBlobs(cv::Mat& srcimg,cvb::CvBlobs& blobs,cvb::CvTracks& tracks,QString outDirCSV,std::string& frameNumberString,double& dMeanBlobArea)
{


    ///// Finding the blobs ////////
     int cnt = 0;

     IplImage fgMaskImg;
///  REGION OF INTEREST - UPDATE - SET
     frameImg =  frame; //Convert The Global frame to lplImage
     fgMaskImg =  srcimg;

    if (bROIChanged || ptROI2.x != 0)
    {
        //Set fLAG sO FROM now on Region of interest is used and cannot be changed.
        bROIChanged = true;
    }
    else
    {
        frameImg =  frame; //Convert The Global frame to lplImage
        fgMaskImg =  srcimg;
    }


   // cout << "Roi Sz:" << vRoi.size() << endl;
    IplImage  *labelImg=cvCreateImage(cvGetSize(&frameImg), IPL_DEPTH_LABEL, 1);
    cvb::cvLabel( &fgMaskImg, labelImg, blobs );

    cvb::cvFilterByROI(vRoi,blobs); //Remove Blobs Outside ROIs
    cvb::cvBlobAreaMeanVar(blobs,dMeanBlobArea,dVarBlobArea);
    double dsigma = 3.0*sqrt(dVarBlobArea);
    cvb::cvFilterByArea(blobs,max(dMeanBlobArea-dsigma,LOW_LOWERBOUND_BLOB_AREA),(unsigned int)max((dMeanBlobArea+3*dsigma),LOW_UPPERBOUND_BLOB_AREA)); //Remove Small Blobs


    //Debug Show Mean Size Var
    //std::cout << dMeanBlobArea <<  " " << dMeanBlobArea+3*sqrt(dVarBlobArea) << endl;
    unsigned int RoiID = 0;
    for (std::vector<cv::Rect>::iterator it = vRoi.begin(); it != vRoi.end(); ++it)
    {
        ltROI iroi = (ltROI)(*it);
        RoiID++;

        //Custom Filtering the blobs for Rendering
        //Count Blobs in ROI
        for (cvb::CvBlobs::const_iterator it = blobs.begin(); it!=blobs.end(); ++it)
        {
            cvb::CvBlob* blob = it->second;
            cv::Point pnt;
            pnt.x = blob->centroid.x;
            pnt.y = blob->centroid.y;

            if (iroi.contains(pnt))
            {
                //cnt++;
                cvb::cvRenderBlob(labelImg, blob, &fgMaskImg, &frameImg, CV_BLOB_RENDER_CENTROID|CV_BLOB_RENDER_BOUNDING_BOX | CV_BLOB_RENDER_COLOR | CV_BLOB_RENDER_AREA, cv::Scalar(0,200,0),0.6);
            }
        }

        //Custom Render Tracks in ROI Loop
        for (cvb::CvTracks::const_iterator it=tracks.begin(); it!=tracks.end(); ++it)
        {
            cv::Point pnt;
            pnt.x = it->second->centroid.x;
            pnt.y = it->second->centroid.y;


            if (iroi.contains(pnt))
                cvRenderTrack(*((*it).second),vLumRec ,it->first ,  &fgMaskImg, &frameImg, CV_TRACK_RENDER_ID | CV_TRACK_RENDER_PATH,&trackFnt );


        }


        //cvSetImageROI(&frameImg, iroi);
        //cvSetImageROI(&fgMaskImg, iroi);
        //cvSetImageROI(labelImg,iroi);
        // render blobs in original image
        //cvb::cvRenderBlobs( labelImg, blobs, &fgMaskImg, &frameImg,CV_BLOB_RENDER_CENTROID|CV_BLOB_RENDER_BOUNDING_BOX | CV_BLOB_RENDER_COLOR);

        //Make File Names For Depending on the Vial - Crude but does the  job
        QString strroiFileN = outDirCSV;
        QString strroiFilePos = outDirCSV;
        char buff[50];
        sprintf(buff,"/V%d_pos_N.csv",RoiID);
        strroiFileN.append(buff);
        sprintf(buff,"/V%d_pos.csv",RoiID);
        strroiFilePos.append(buff);

        saveTrackedBlobs(blobs,strroiFilePos,frameNumberString,iroi);
        cnt += saveTrackedBlobsTotals(blobs,tracks,strroiFileN,frameNumberString,iroi);
    } //For Each ROI

    //Save to Disk
    if (bTracking && bSaveImages)
    {
        saveImage(frameNumberString,outDirCSV,frame);
        cv::putText(frame, "Save ON", cv::Point(15, 600),
                cv::FONT_HERSHEY_SIMPLEX, 0.5 , cv::Scalar(0,0,0));

    }



    // *always* remember freeing unused IplImages
    cvReleaseImage( &labelImg );

    return cnt;
}

int saveTrackedBlobs(cvb::CvBlobs& blobs,QString filename,std::string frameNumber,cv::Rect& roi)
{
    int cnt = 0;
    int Vcnt = 1;
    bool bNewFileFlag = true;

    //Loop Over ROI
    Vcnt++; //Vial Count
    cnt = 0;

    QFile data(filename);
    if (data.exists())
        bNewFileFlag = false;

    if(data.open(QFile::WriteOnly |QFile::Append))
    {
        QTextStream output(&data);
        if (bNewFileFlag)
             output << "frameN,SerialN,BlobLabel,Centroid_X,Centroid_Y,Area" << endl;

        //Loop Over Blobs
        for (cvb::CvBlobs::const_iterator it=blobs.begin(); it!=blobs.end(); ++it)
        {

            cvb::CvBlob* cvB = it->second;
            cv::Point pnt;
            pnt.x = cvB->centroid.x;
            pnt.y = cvB->centroid.y;

            cnt++;

            if (roi.contains(pnt))
                //Printing the position information
                output << frameNumber.c_str() << "," << cnt <<","<< cvB->label << "," << cvB->centroid.x <<","<< cvB->centroid.y  <<","<< cvB->area  <<  endl;
          }


       data.close();

      }


    return cnt;
}

//Saves the total Number of Counted Blobs and Tracks only
int saveTrackedBlobsTotals(cvb::CvBlobs& blobs,cvb::CvTracks& tracks,QString filename,std::string frameNumber,ltROI& roi)
{

    bool bNewFileFlag = true;
    int cnt = 0;
    int Larvacnt = 0;
    cnt++;
    //cv::Rect iroi = (cv::Rect)(*it);

    QFile data(filename);
    if (data.exists())
        bNewFileFlag = false;

    if(data.open(QFile::WriteOnly |QFile::Append))
    {

        int blobCount = 0;
        int trackCount = 0;

        //Count Blobs in ROI
        for (cvb::CvBlobs::const_iterator it = blobs.begin(); it!=blobs.end(); ++it)
        {
            cvb::CvBlob* blob = it->second;
            cv::Point pnt;
            pnt.x = blob->centroid.x;
            pnt.y = blob->centroid.y;

            if (roi.contains(pnt))
                blobCount++;
        }

        //Count Tracks in ROI
        for (cvb::CvTracks::const_iterator it = tracks.begin(); it!=tracks.end(); ++it)
        {
            cvb::CvTrack* track = it->second;
            cv::Point pnt;
            pnt.x = track->centroid.x;
            pnt.y = track->centroid.y;

            if (roi.contains(pnt))
                trackCount++;
        }


        QTextStream output(&data);
        if (bNewFileFlag)
             output << "frameN,blobN,TracksN" << endl;

        output << frameNumber.c_str() << "," << blobCount << "," << trackCount << endl;
        Larvacnt +=blobCount;
        data.close();
    }


    return Larvacnt;
}


//std::vector<cvb::CvBlob*> getBlobsinROI(cvb::CvBlobs& blobs)
//{
    //std::vector<cvb::CvBlob*> *vfiltBlobs = new std::vector<cvb::CvBlob*>((blobs.size()));

   // return 0;

//}


ltROI* ltGetFirstROIContainingPoint(ltROIlist& vRoi ,cv::Point pnt)
{
    for (ltROIlist::iterator it = vRoi.begin(); it != vRoi.end(); ++it)
    {
        ltROI* iroi = &(*it);
        if (iroi->contains(pnt))
                return iroi;
    }

    return 0; //Couldn't find it
}


int saveTracks(cvb::CvTracks& tracks,QString filename,std::string frameNumber)
{
    bool bNewFileFlag = true;
    int cnt;
    int Vcnt = 0;

    //Loop Over ROI
    for (ltROIlist::iterator it = vRoi.begin(); it != vRoi.end(); ++it)
    {
        cnt = 1;
        Vcnt++;
        ltROI iroi = (ltROI)(*it);
        QString strroiFile = filename.left(filename.lastIndexOf("/"));
        char buff[50];
        sprintf(buff,"/V%d_pos_tracks.csv",Vcnt);
        strroiFile.append(buff);

        QFile data(strroiFile);
        if (data.exists())
            bNewFileFlag = false;



        if(data.open(QFile::WriteOnly |QFile::Append))
        {

            QTextStream output(&data);
            if (bNewFileFlag)
                 output << "frameN,TrackID,TrackBlobLabel,Centroid_X,Centroid_Y,Lifetime,Active,Inactive" << endl;

            //Save Tracks In ROI
            for (cvb::CvTracks::const_iterator it=tracks.begin(); it!=tracks.end(); ++it)
            {
                cnt++;
                cvb::CvTrack* cvT = it->second;
                //cvb::CvLabel cvL = it->first;

                cv::Point pnt;
                pnt.x = cvT->centroid.x;
                pnt.y = cvT->centroid.y;

                if (iroi.contains(pnt))
                    //Printing the position information +
                    //+ lifetime; ///< Indicates how much frames the object has been in scene.
                    //+active; ///< Indicates number of frames that has been active from last inactive period.
                    //+ inactive; ///< Indicates number of frames that has been missing.
                    output << frameNumber.c_str()  << "," << cvT->id  << "," << cvT->label  << "," << cvT->centroid.x << "," << cvT->centroid.y << "," << cvT->lifetime  << "," << cvT->active  << "," << cvT->inactive <<  endl;
              }
            }
        data.close();

   } //Loop ROI
     return cnt;
}


unsigned int readBiolumFile(std::vector<unsigned int> &vBioLumRec,QString filename,unsigned int& imaxValue)
{

     imaxValue = 0;
     unsigned int irecCount=0;

     unsigned int isample;
     QString ssample;

     QFile data(filename);
     if (!data.exists())
     {
         cerr << "Could not open bioLum File" << endl;
         exit(1);
     }

    if(data.open(QFile::ReadOnly))
    {
        QTextStream input(&data);

        while (!input.atEnd()) //Read In All Lines And copy values into Vector
        {
            irecCount++; //Convert Text Num to int
            input >> ssample;
            isample = ssample.toUInt();
            vBioLumRec.push_back(isample);

            //Rec Max Value
            if (imaxValue < isample)
                imaxValue = isample;
        }
    }

return irecCount;
}


//Mouse Call Back Function
void CallBackFunc(int event, int x, int y, int flags, void* userdata)
{
     if  ( event == cv::EVENT_LBUTTONDOWN )
     {
        bMouseLButtonDown = true;
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

                addROI(newROI);
                drawROI();
                b1stPointSet = false; //Rotate To 1st Point Again
             }
        }


        cout << "Left button of the mouse is clicked - position (" << x << ", " << y << ")" << endl;
     }

     if (event == cv::EVENT_LBUTTONUP)
     {
        bMouseLButtonDown = false;
     }
     else if  ( event == cv::EVENT_RBUTTONDOWN )
     {
        cv::Point mousepnt;
        mousepnt.x = x;
        mousepnt.y = y;
        cout << "Right button of the mouse is clicked - Delete ROI position (" << x << ", " << y << ")" << endl;

        if (bPaused && !bROIChanged)
        {
            deleteROI(mousepnt);
            drawROI();
        }
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

void addROI(ltROI& newRoi)
{
    //std::vector<cv::Rect>::iterator it= vRoi.end();
    //vRoi.insert(it,newRoi);
    vRoi.push_back(newRoi);
    //Draw the 2 points
    cv::circle(frame,ptROI1,3,cv::Scalar(255,0,0),1);
    cv::circle(frame,ptROI2,3,cv::Scalar(255,0,0),1);

    cout << "Added, total:" << vRoi.size() << endl;

}

void deleteROI(cv::Point mousePos)
{
    std::vector<cv::Rect>::iterator it = vRoi.begin();

    while(it != vRoi.end())
    {
        ltROI* roi=&(*it);

        if (roi->contains(mousePos))
        {
            std::vector<ltROI>::iterator tmp = it;
            vRoi.erase(tmp);
            cout << "Deleted:" << roi->x << " " << roi->y << endl;
            break;
        }
         ++it;

    }

}

void drawROI()
{
    frameCpy.copyTo(frame); //Restore Original IMage
    for (std::vector<ltROI>::iterator it = vRoi.begin(); it != vRoi.end(); ++it) {

        ltROI iroi = (ltROI)(*it);
         cv::rectangle(frame,iroi,cv::Scalar(0,0,250));

         if (bTracking)
         {
             cv::Point pt1,pt2;
             pt1.x = iroi.x;
             pt1.y = iroi.y;
             pt2.x = pt1.x + iroi.width;
             pt2.y = pt1.y + iroi.height;

             cv::circle(frame,pt1 ,3,cv::Scalar(255,0,0),1);
             cv::circle(frame,pt2,3,cv::Scalar(255,0,0),1);


         }
    }
}
