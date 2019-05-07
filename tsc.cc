#include <iostream>
#include <memory>
#include <thread>
#include <vector>
#include <string>
#include <unistd.h>
#include <grpc++/grpc++.h>
#include "client.h"

#include "sns.grpc.pb.h"
using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;
using csce438::Message;
using csce438::ListReply;
using csce438::Request;
using csce438::Reply;
using csce438::ServerInfo;
using csce438::SNSService;
using csce438::RoutingService;

Message MakeMessage(const std::string& username, const std::string& msg)
{
    Message m;
    m.set_username(username);
    m.set_msg(msg);
    google::protobuf::Timestamp* timestamp = new google::protobuf::Timestamp();
    timestamp->set_seconds(time(NULL));
    timestamp->set_nanos(0);
    m.set_allocated_timestamp(timestamp);
    return m;
}

class Client : public IClient
{
    public:
        Client(const std::string& hname,
               const std::string& uname,
               const std::string& p)
            :hostname(hname), username(uname), port(p)
            {}
    protected:
        virtual int connectTo();
        virtual IReply processCommand(std::string& input);
        virtual void processTimeline();
    private:
        std::string hostname;
        std::string username;
        std::string port;
        
        
        std::unique_ptr<SNSService::Stub> stub_;

        
        std::unique_ptr<RoutingService::Stub> routingStub_;
        std::string availableHostname;
        std::string availablePort;
        int Find_Available_Server();
        int checkServer(); 
		
        IReply Login();
        IReply List();
        IReply Follow(const std::string& username2);
        IReply UnFollow(const std::string& username2);
        void Timeline(const std::string& username, bool firstTime);


};

int main(int argc, char** argv)
{
    std::string hostname = "localhost";
    std::string username = "default";
    std::string port = "3010";
    int opt = 0;
    while ((opt = getopt(argc, argv, "h:u:p:")) != -1)
    {
        switch(opt)
        {
            case 'h':
                hostname = optarg;break;
            case 'u':
                username = optarg;break;
            case 'p':
                port = optarg;break;
            default:
                std::cerr << "Invalid Command Line Argument\n";
        }
    }

    Client myc(hostname, username, port);
    
    myc.run_client();

    return 0;
}

int Client::connectTo()
{
    
    std::string routingServerAddress = hostname + ":" + port;
    routingStub_ = std::unique_ptr<RoutingService::Stub>(RoutingService::NewStub(grpc::CreateChannel(routingServerAddress, grpc::InsecureChannelCredentials())));

    
    int status = -1;
    while(status != 1)
    {
        
        std::cout << "Looking for available server..." << std::endl;
        status = Find_Available_Server();
        if (status == 1)
        {
            std::cout << "Server detected." << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    std::string address = availableHostname + ":" + availablePort;
    stub_ = std::unique_ptr<SNSService::Stub>(SNSService::NewStub(grpc::CreateChannel(address, grpc::InsecureChannelCredentials())));
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    IReply ire = Login();
    if(!ire.grpc_status.ok())
    {
        return -1;
    }
    return 1;
}

int Client::checkServer()
{
    ClientContext context;
    Request request;
    Reply reply;

    request.set_username(username);
    request.add_arguments("request - server");
    int status = -1;
    bool done = false;
    while(!done){
    Status serverStatus = stub_->Check(&context, request, &reply);
    	if(!serverStatus.ok())
    	{
    		std::cout << "waiting for available server . . .";
        	status = Find_Available_Server();
        	if (status == 1)
        	{
            	std::cout << "available server found!" << std::endl;
            	done = true;
        	}
        	std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        	std::string address = availableHostname + ":" + availablePort;
    		stub_ = std::unique_ptr<SNSService::Stub>(SNSService::NewStub(grpc::CreateChannel(address, grpc::InsecureChannelCredentials())));

    		IReply ire = Login();
    		if(!ire.grpc_status.ok())
    		{
        		return -1;
    		}
    	}
    	else {
    		done = true;
    	}
    }
	

    return 1;
}

int Client::Find_Available_Server()
{
    ClientContext context;
    Request request;
    ServerInfo server;

    request.set_username(username);
    request.add_arguments("request - server");

    Status status = routingStub_->Find_Available_Server(&context, request, &server);
    if(!status.ok())
    {
        return -1;
    }

    availableHostname = server.hostname();
    availablePort = server.port();

    return 1;
}

IReply Client::processCommand(std::string& input)
{
	

	
	
std::string address2 = availableHostname + ":" + availablePort;
stub_ = std::unique_ptr<SNSService::Stub>(SNSService::NewStub(grpc::CreateChannel(address2, grpc::InsecureChannelCredentials())));
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    IReply ire2 = Login();
    if(!ire2.grpc_status.ok())
    {	
    	std::cout << "LOGIN ERROR" << std::endl;
        
    }
    
    
    IReply ire;
    std::size_t index = input.find_first_of(" ");
    if (index != std::string::npos)
    {
        std::string cmd = input.substr(0, index);

        

        std::string argument = input.substr(index+1, (input.length()-index));

        if (cmd == "FOLLOW")
        {
            return Follow(argument);
        }
        else if(cmd == "UNFOLLOW")
        {
            return UnFollow(argument);
        }
    }
    else
    {
        if (input == "LIST")
        {
            return List();
        }
        else if (input == "TIMELINE")
        {
            ire.comm_status = SUCCESS;
            return ire;
        }
    }

    ire.comm_status = FAILURE_INVALID;
    return ire;
}

void Client::processTimeline()
{
	bool firstTime = true;
	while (1){
    	Timeline(username, firstTime);
		firstTime = false;
		std::string address2 = availableHostname + ":" + availablePort;
    	stub_ = std::unique_ptr<SNSService::Stub>(SNSService::NewStub(grpc::CreateChannel(address2, grpc::InsecureChannelCredentials())));
    	std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    	
    	IReply ire2 = Login();
    	if(!ire2.grpc_status.ok())
    	{	
        	break;
    	}
    }
	
   
}

IReply Client::List()
{
    
    Request request;
    request.set_username(username);

    
    ListReply list_reply;

    
    ClientContext context;

    Status status = stub_->List(&context, request, &list_reply);
    IReply ire;
    ire.grpc_status = status;
    
    
    if(status.ok())
    {
        ire.comm_status = SUCCESS;
        std::string all_users;
        std::string following_users;
        for(std::string s : list_reply.all_users())
        {
            ire.all_users.push_back(s);
        }
        for(std::string s : list_reply.followers())
        {
            ire.followers.push_back(s);
        }
    }
    return ire;
}
        
IReply Client::Follow(const std::string& username2)
{
    Request request;
    request.set_username(username);
    request.add_arguments(username2);

    Reply reply;
    ClientContext context;

    Status status = stub_->Follow(&context, request, &reply);
    IReply ire; ire.grpc_status = status;
	

    if (reply.msg() == "Join Failed -- Invalid Username")
    {
        ire.comm_status = FAILURE_INVALID_USERNAME;
    }
    else if (reply.msg() == "Join Failed -- Invalid Username")
    {
        ire.comm_status = FAILURE_INVALID_USERNAME;
    }
    else if (reply.msg() == "Join Failed -- Already Following User")
    {
        ire.comm_status = FAILURE_ALREADY_EXISTS;
    }
    else if (reply.msg() == "Join Successful")
    {
        ire.comm_status = SUCCESS;
    }
    else
    {
        ire.comm_status = FAILURE_UNKNOWN;
    }
    return ire;
}

IReply Client::UnFollow(const std::string& username2)
{
    Request request;

    request.set_username(username);
    request.add_arguments(username2);

    Reply reply;

    ClientContext context;

    Status status = stub_->UnFollow(&context, request, &reply);
    IReply ire;
    ire.grpc_status = status;
    if (reply.msg() == "Leave Failed -- Invalid Username")
    {
        ire.comm_status = FAILURE_INVALID_USERNAME;
    }
    else if (reply.msg() == "Leave Failed -- Not Following User")
    {
        ire.comm_status = FAILURE_INVALID_USERNAME;
    }
    else if (reply.msg() == "Leave Successful")
    {
        ire.comm_status = SUCCESS;
    }
    else
    {
        ire.comm_status = FAILURE_UNKNOWN;
    }

    return ire;
}

IReply Client::Login()
{
    Request request;
    request.set_username(username);
    Reply reply;
    ClientContext context;

    Status status = stub_->Login(&context, request, &reply);

    IReply ire;
    ire.grpc_status = status;
    if (reply.msg() == "Invalid Username")
    {
        ire.comm_status = FAILURE_ALREADY_EXISTS;
    }
    
    else if (reply.msg() == "Login Successful!")
    {
        ire.comm_status = SUCCESS;
    }
    return ire;
}

void Client::Timeline(const std::string& username, bool firstTime)
{
    ClientContext context;
    
    std::shared_ptr<ClientReaderWriter<Message, Message>> stream(stub_->Timeline(&context));

    
    std::thread writer([username, stream, firstTime]()
        {	
        	Message m;
        	std::string input;
        	if(firstTime){
            	input = "Set Stream";
            	m = MakeMessage(username, input);
            	stream->Write(m);
            }
            else{
            	input = "Reconnecting";
            	m = MakeMessage(username, input);
            	stream->Write(m);
            }
            while (1)
            {
                input = getPostMessage();
                m = MakeMessage(username, input);
                if(!stream->Write(m)){
                	Status status = stream->Finish();
                	break;
                }
            }
            stream->WritesDone();
        });

    std::thread reader([username, stream]()
        {
            Message m;
            while(stream->Read(&m))
            {
                google::protobuf::Timestamp temptime = m.timestamp();
                std::time_t time = temptime.seconds();
                if(m.username() == "")
                {
                	Print_Bulk_Message(m.msg());
                }
                else
                {
                	displayPostMessage(m.username(), m.msg(), time);
                }
            }
        });

    
    writer.join();
    reader.join();
}

