
#include "MessageIdentifiers.h"
#include "RakPeerInterface.h"

#include <iostream>
#include <cstring>	
#include <string>
#include <stdio.h>


using namespace std;


static unsigned int SERVER_PORT = 65000;
static unsigned int CLIENT_PORT = 65001;
static unsigned int MAX_CONNECTIONS = 4;
char* IPConnection = "127.0.0.1";



int main() {

	RakNet::RakPeerInterface *rakPeerInterface = RakNet::RakPeerInterface::GetInstance();

	char userInput[255];
	char ip[64];
	std::cout << "Press (s) for server" << std::endl;
	std::cout << "Press (c) for client" << std::endl;
	bool selectedRole = false;

	while (!selectedRole) {
		std::cin >> userInput;
		if (userInput[0] == 's') {

			RakNet::SocketDescriptor socketDescriptors[1];
			socketDescriptors[0].port = SERVER_PORT;
			socketDescriptors[0].socketFamily = AF_INET; // Test out IPV4

			//socketDescriptors[1].port = atoi(portstring);
			//socketDescriptors[1].socketFamily = AF_INET6; // Test out IPV6

			bool isSuccessful = rakPeerInterface->Startup(MAX_CONNECTIONS, socketDescriptors, 1) == RakNet::RAKNET_STARTED;
			assert(isSuccessful); //crash if server cannot start

			//ensures we are the server
			rakPeerInterface->SetMaximumIncomingConnections(MAX_CONNECTIONS);
			std::cout << "Server started " << std::endl;
			selectedRole = true;
		}
		else if (userInput[0] == 'c') {
			std::cout << "Connecting as client" << std::endl;

			RakNet::SocketDescriptor socketDescriptor(CLIENT_PORT, 0);
			socketDescriptor.socketFamily = AF_INET;
			rakPeerInterface->Startup(8, &socketDescriptor, 1);
			rakPeerInterface->SetOccasionalPing(true);

			strcpy(ip, IPConnection);
		#if LIBCAT_SECURITY==1
			char public_key[cat::EasyHandshake::PUBLIC_KEY_BYTES];
			FILE *fp = fopen("publicKey.dat", "rb");
			fread(public_key, sizeof(public_key), 1, fp);
			fclose(fp);
		#endif

		#if LIBCAT_SECURITY==1
			RakNet::PublicKey pk;
			pk.remoteServerPublicKey = public_key;
			pk.publicKeyMode = RakNet::PKM_USE_KNOWN_PUBLIC_KEY;
			bool b = rakPeerInterface->Connect(ip, SERVER_PORT, "hunter2", (int)strlen("hunter2"), &pk) == RakNet::CONNECTION_ATTEMPT_STARTED;
		#else
			RakNet::ConnectionAttemptResult car = rakPeerInterface->Connect(ip, SERVER_PORT, "hunter2", (int)strlen("hunter2"));
			RakAssert(car == RakNet::CONNECTION_ATTEMPT_STARTED);
		#endif


			std::cout << "Connected as client" << std::endl;
			selectedRole = true;
		}
		else {
			std::cout << "Not a valid command" << std::endl;
		}
	} //end of while
	userInput[0] = ' ';
	std::cout << "Press q to exit" << std::endl;
	while (userInput[0] != 'q' && !userInput[1]) {
		std::cin >> userInput;
	}

	return 0;
}