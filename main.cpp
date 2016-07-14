/* 
 * File:   main.cpp
 * Author: mayur
 *
 * Created on 14 July, 2016, 5:50 PM
 */

#include <cstdlib>
#include <iostream>
#include <Poco/NotificationQueue.h>
#include <Poco/ThreadPool.h>
#include <Poco/Thread.h>
#include <Poco/Mutex.h>
#include <Poco/URI.h>
#include <Poco/Net/HTTPClientSession.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPMessage.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/StreamCopier.h>

using Poco::NotificationQueue;
using Poco::ThreadPool;
using Poco::Notification;
using Poco::Thread;
using Poco::FastMutex;
using Poco::URI;
using Poco::Net::HTTPClientSession;
using Poco::Net::HTTPRequest;
using Poco::Net::HTTPMessage;
using Poco::Net::HTTPResponse;
using Poco::StreamCopier;
using Poco::AutoPtr;
/*
 * 
 * 
 */

/*
 * Class to hold the response object.
 * The response string will be the one obtained from jsonplaceholder.
 */
class Response {
public:
    Response(std::string s){
        resStr = s;
    }
    void setResponseString(std::string s){
        resStr = s;
    }
    std::string getResponseString(){
        return resStr;
    }
private:
    std::string resStr;
};

/*
 * ResponseNotification class derives the Poco::Notification class.
 * These objects are notifications to the NotificationQueue. 
 */

class ResponseNotification: public Notification
        // The notification sent to worker threads.
{
public:
        typedef AutoPtr<ResponseNotification> Ptr;

        ResponseNotification(Response *res):
                response(res)
        {
        }
        Response* getResponse() const{
            return response;
        }
private:
        Response* response;
};

/*
 * Request class holds the requestID and the address of the response queue where the response needs to be sent.
 * When the subscriber thread receives the response, it will notify the observer thread on this responseQueue. 
 */

class Request {
public:
    int id;
    Poco::NotificationQueue* resQ;
    std::string observerName;
    Request(int reqID, Poco::NotificationQueue* resQueue, std::string obsName):
    id(reqID),
    resQ(resQueue),
    observerName(obsName)
    {
    }
};


/*
 * RequestNotification class derives the Poco::Notification class.
 * These objects are notifications to the NotificationQueue.
 */

class RequestNotification: public Poco::Notification{
public:
    typedef Poco::AutoPtr<RequestNotification> Ptr;
    RequestNotification(Request* req):
    request(req)
    {
    }
    Request* getRequest(){
        return request;
    }
private:
    Request* request;
};


/*
 * The Post class is used for performing the GET/UPDATE/DELETE/POST operations.
 * The doMethod functions only queues the request objects to the Request Notification queue and returns.
 * The subscriber threads waiting on the requestNotification queue picks up the Request notification.
 * The actual sending and receiving of response is done in the Publisher thread.
 */

class Post{
public:
    Post(NotificationQueue& requestQ):
    reqQ(requestQ)
    {
    }
    int doGet(int id, NotificationQueue* resQ, std::string obsName){
        //std::cout<<"Called doGet with id : "<<id<<" and responseQueue address: "<<resQ<<" and request queue address is : "<<&reqQ<<std::endl;
        reqQ.enqueueNotification(new RequestNotification(new Request(id, resQ, obsName)));
        return 0;
    }
private:
    NotificationQueue& reqQ;
};

/*
 * Mutex to synchronize printing between multiple threads.
 */
class CommonMutex{
public:
    static FastMutex commonMutex;
};

FastMutex CommonMutex::commonMutex;


/*
 * Observer thread class. This will call the API and wait on their response queue for response notifications.
 */

class Observer : public Poco::Runnable{
public:
    Observer(const std::string& name, NotificationQueue* resQ, NotificationQueue& reqQ, Post& p):
                _name(name),
                _resQ(resQ),
                _reqQ(reqQ),
                 post(p)
    {
    }               
    virtual void run() {
        //std::cout<<"Observer thread created"<<std::endl;
        //std::cout<<"Name is : "<<_name<<" request queue address is : "<<&_reqQ<<" resQ address is : "<<_resQ<<std::endl;
        post.doGet(80, _resQ, _name);
        post.doGet(90, _resQ, _name);
        post.doGet(10, _resQ, _name);
        post.doGet(30, _resQ, _name);
        post.doGet(35, _resQ, _name);
        while(1){
            Notification::Ptr pNf(_resQ->waitDequeueNotification());
            if(pNf){
                ResponseNotification::Ptr pResponseNf = pNf.cast<ResponseNotification>();
                if (pResponseNf){
                    FastMutex::ScopedLock lock(CommonMutex::commonMutex);
                    Response* res = pResponseNf->getResponse();
                    std::cout << _name << " got response " << res->getResponseString() << std::endl;

                }
            }
            else
                break;
        }
    }
private:
    std::string        _name;
    NotificationQueue* _resQ;
    NotificationQueue& _reqQ;
    Post& post;
    static FastMutex observerMutex;
};

FastMutex Observer::observerMutex;


/*
 * Publisher Thread class. These threads will wait for Request Notifications.
 * Once obtained, this will do the actual request and queue the response in the Response Notification queue
 * which is obtained from the request object. 
 */


class Publisher : public Poco::Runnable{
public:
    Publisher(const std::string& name, NotificationQueue& queue):
                _name(name),
                _queue(queue)
    {
    }   
    virtual void run(){
        while(1){
            //std::cout<<"Publisher thread created"<<std::endl;
            //std::cout<<"Name is "<<_name<<" request queue address is : "<<&_queue<<std::endl;
            Notification::Ptr pNf(_queue.waitDequeueNotification());
            if(pNf){
                RequestNotification::Ptr pRequestNf = pNf.cast<RequestNotification>();
                if(pRequestNf){
                    //FastMutex::ScopedLock lock(publisherMutex);
                    Request *req = pRequestNf->getRequest();
                    NotificationQueue* responseQueue = req->resQ;
                    int reqID = req->id;
                    {
                        FastMutex::ScopedLock lock(CommonMutex::commonMutex);
                        std::cout<< _name<<" got request with id : "<<reqID<<" from "<<req->observerName<<std::endl;
                    }
                    //delete[] req;
                    std::string result = getResponse(reqID);
                    responseQueue->enqueueNotification(new ResponseNotification(new Response(result)));
                    //std::cout << _name << " got request with id " << req->id <<" and response queue address for it is : "<<req->resQ<< std::endl;
                }
            }
            else
                break;
        }
    }
    
    std::string getResponse(int id){
        std::string result;
        URI uri("http://jsonplaceholder.typicode.com/posts/" + std::to_string(id));
        std::string path(uri.getPathAndQuery());
        HTTPClientSession session(uri.getHost(), uri.getPort());
        HTTPRequest request(HTTPRequest::HTTP_GET, path, HTTPMessage::HTTP_1_1);
        HTTPResponse response;
        session.sendRequest(request);
        std::istream& rs = session.receiveResponse(response);
        if(response.getStatus() == Poco::Net::HTTPResponse::HTTP_OK){
            StreamCopier::copyToString(rs, result);
            return result;
        }
        else
            return "";
    }
    
private:
    std::string        _name;
    NotificationQueue& _queue;
    static FastMutex publisherMutex;
};

FastMutex Publisher::publisherMutex;

/*
 * Entry function main.
 * Publishers and Observers can be created here.
 * There will be a NotificationQueue for each observer and only one NotificationQueue for Publisher.
 */

int main(int argc, char** argv) {
    
    NotificationQueue reqQ;
    NotificationQueue *resQ = new NotificationQueue;
    NotificationQueue *resQ2 = new NotificationQueue;
    
    std::cout<<"In main, request queue address is : "<<&reqQ<<" response queue address is : "<<resQ<<std::endl;
    
    Post po(reqQ);
    Publisher pub1("Publisher1", reqQ);
    //Publisher pub2("Publisher2", reqQ);
    Observer observer1("Observer1", resQ, reqQ, po);
    Observer observer2("Observer2", resQ2, reqQ, po);
    
    ThreadPool::defaultPool().start(pub1);
    //ThreadPool::defaultPool().start(pub2);
    ThreadPool::defaultPool().start(observer1);
    ThreadPool::defaultPool().start(observer2);
    
    Thread::sleep(6000);
    reqQ.wakeUpAll();
    resQ->wakeUpAll();
    resQ2->wakeUpAll();
    ThreadPool::defaultPool().joinAll();

    return 0;
}

