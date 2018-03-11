#include "MessageIdentifiers.h"
#include "RakPeerInterface.h"
#include "BitStream.h"

#include "Player.h"

#include <vector>
#include <string>
#include <iostream>
#include <thread>
#include <chrono>

static unsigned int SERVER_PORT = 65000;
static unsigned int CLIENT_PORT = 65001;
static unsigned int MAX_CONNECTIONS = 4;

enum NetworkState
{
	NS_Init = 0,
	NS_PendingStart,
	NS_Started,
	NS_Lobby,
	NS_Pending,
	NS_Ready,
	NS_Waiting,
	NS_PlayerTurn,
	NS_End,
};

//
enum ServerState
{
	SS_Null = 0,
	SS_Waiting,
	SS_Running,
	SS_Stopped
};

enum MoveType
{
	Attack,
	Heal
};

//IDs of things sent peers/server
enum {
	ID_TOSERVER_LOBBY_READY = ID_USER_PACKET_ENUM,
	ID_TOSERVER_PLAYERMOVE,
	ID_TOSERVER_PLAYERDEATH,
	ID_TOSERVER_REQUESTOTHERSTATS,


	ID_TOCLIENT_CLEARSCREEN,
	ID_TOCLIENT_CLEARLINE,
	ID_TOCLIENT_SHOWSTATS,
	ID_TOCLIENT_SHOWOTHERSTATS,
	ID_TOCLIENT_READY,
	ID_TOCLIENT_WAITING,
	ID_TOCLIENT_STARTTURN,
	ID_TOCLIENT_ATTACKTARGET,
	ID_TOCLIENT_HEALTARGET,
	ID_TOCLIENT_INVALIDNAME,
	ID_TOCLIENT_TAKEDAMAGE,
	ID_TOCLIENT_HEALEDBY,
	ID_TOCLIENT_WATCHCOMBAT, 
	ID_TOCLIENT_INFORMPLAYERDEATH,
	ID_TOCLIENT_CHARACTERDEATH,
	ID_TOCLIENT_ENDGAME,

	ID_TOCLIENT_PEERCONNECTED,//
	ID_TOCLIENT_PEERCONNECTIONLOST
};

//SERVER FUNCTIONS



bool isServer = false;
bool isRunning = true;

RakNet::RakPeerInterface *g_rakPeerInterface = nullptr;

RakNet::SystemAddress g_serverAddress; //only used for clients
NetworkState g_networkState = NS_Init;
ServerState g_serverState = SS_Null; //no one is the server at beginning


//holds GUID, Player data
std::vector<RakNet::RakNetGUID> m_playerGUID;
std::vector<Player> m_player;
int totalPlayers = 0;
int activePlayers = 0;
int minPlayers = 2;
int turn = 0;


//SERVER FUNCTIONS
void ServerOnIncomingConnection(RakNet::Packet* packet);
void ServerReceiveNewPlayer(RakNet::Packet* packet);
void ServerOnConnectionLost(RakNet::Packet* packet);

bool ServerCheckPlayerCount(); //total > min amount && all player ready
void ServerReadyPlayers(); //Moves players into the game

bool ServerPlayersStillAlive(); //Check for 2+ active players
void ServerFindNextPlayerTurn(); 
void ServerProcessMove(RakNet::Packet* packet);
void ServerProcessPlayerDeath(RakNet::Packet* packet);

void ServerClearClientScreen();
void ServerClearClientLine();
void ServerShowClientStats(); 
void ServerShowClientStats(RakNet::RakNetGUID playerGUID);
void ServerShowOtherClientStats(RakNet::Packet* packet);
void ServerEndGame();
//Client Functions
void ClientOnConnectionAccepted(RakNet::Packet* packet);
void ClientOnPeerConnectionLost(RakNet::Packet* packet);

void ClientSetPlayerReady(RakNet::Packet* packet);
void ClientSetPlayerWaiting(RakNet::Packet* packet);
void ClientSetPlayerStartTurn(RakNet::Packet* packet);

void ClientPlayerAttackTarget(RakNet::Packet* packet);
void ClientPlayerHealTarget(RakNet::Packet* packet);
void ClientPlayerInvalidName(RakNet::Packet* packet);

void ClientPlayerTookDamage(RakNet::Packet* packet);
void ClientPlayerHealedBy(RakNet::Packet* packet);

void ClientPlayerWatchCombat(RakNet::Packet* packet);
void ClientPlayerInformDeath(RakNet::Packet* packet);

void ClientClearScreen(RakNet::Packet* packet);
void ClientClearLine(RakNet::Packet* packet);
void ClientShowPlayerStats(RakNet::Packet* packet);
void ClientRequestOtherStats();
void ClientShowOtherPlayerStats(RakNet::Packet* packet);

void ClientPlayerCharacterDeath(RakNet::Packet* packet);
void ClientEndGame(RakNet::Packet* packet);

//Packet reader
unsigned char GetPacketIdentifier(RakNet::Packet *packet);
bool HandleLowLevelPackets(RakNet::Packet* packet);
void PacketHandler();
void InputHandler();


void ServerOnIncomingConnection(RakNet::Packet* packet) {
	assert(isServer);
	
	m_playerGUID.push_back(packet->guid);
	m_player.push_back(Player());
	totalPlayers++;

	std::cout << "Total Players: " << totalPlayers << std::endl;
}

//Checks if everyone has a name/is ready
bool ServerCheckPlayerCount() {
	if (totalPlayers >= minPlayers) {
		for (int i = 0; i < totalPlayers; i++) {
			if (!m_player[i].ready)
				return false;
			else
				continue;
		}
		std::cout << "All Players Ready" << std::endl;
		activePlayers = totalPlayers;
		ServerClearClientScreen();
		return true;
	}
	return false;
}

//goes through all players and checks if there are still two alive
bool ServerPlayersStillAlive() {
	if (activePlayers >= 2)
		return true;
	else
		return false;
}

void ServerEndGame() {

	//packet info: id, victor name

	RakNet::BitStream bs;
	bs.Write((RakNet::MessageID)ID_TOCLIENT_ENDGAME);


	//get victor id
	for (int i = 0; i < totalPlayers; i++) 
		if (m_player[i].health > 0) 
			bs.Write(m_player[i].name.c_str());
	for (int i = 0; i < totalPlayers; i++)
		assert(g_rakPeerInterface->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, m_playerGUID[i], false));
	
	isRunning = false;
}

void ServerFindNextPlayerTurn() {

	//packet info= id, numPlayers, player1, player2...
	//lists all the available characters to attack
	
	//check if there's anyone  available
	if (!ServerPlayersStillAlive()) {
		ServerEndGame();
		return;
	}
	
	//who's turn is it?
	bool foundNextPlayer = false;
	while (!foundNextPlayer) {
		turn++;
		if (turn >= totalPlayers)
			turn = 0;

		if (m_player[turn].health > 0)
			foundNextPlayer = true;
	}

	ServerClearClientLine();
	ServerShowClientStats();

	RakNet::BitStream ToClientAttacker;
	ToClientAttacker.Write((RakNet::MessageID)ID_TOCLIENT_STARTTURN);
	ToClientAttacker.Write(activePlayers);
	//write the amount of players you can hit
	for (int i = 0; i < totalPlayers; i++) {
		if (i == turn || m_player[i].health < 0)
			continue;
		if (m_player[i].health > 0) {
			ToClientAttacker.Write(m_player[i].name.c_str());
		}
	}
	assert(g_rakPeerInterface->Send(&ToClientAttacker, HIGH_PRIORITY, RELIABLE_ORDERED, 0, m_playerGUID[turn], false));

	//sending who's turn it is to the other players
	//packet info: MessageID, CurrentPlayer's Name, Class, Damage, Heal, Health and MaxHealth
	RakNet::BitStream ToClientOthers;
	ToClientOthers.Write((RakNet::MessageID)ID_TOCLIENT_WAITING);
	ToClientOthers.Write((RakNet::RakString)m_player[turn].name.c_str());
	ToClientOthers.Write((RakNet::RakString)m_player[turn].charClass.c_str());
	ToClientOthers.Write(m_player[turn].damage);
	ToClientOthers.Write(m_player[turn].heal);
	ToClientOthers.Write(m_player[turn].health);
	ToClientOthers.Write(m_player[turn].maxHealth);
	for (int i = 0; i < totalPlayers; i++)
		if (i != turn)
			assert(g_rakPeerInterface->Send(&ToClientOthers, HIGH_PRIORITY, RELIABLE_ORDERED, 0, m_playerGUID[i], false));
	
	
}

void ServerReadyPlayers() {
	RakNet::BitStream bs;
	bs.Write((RakNet::MessageID)ID_TOCLIENT_READY);
	for (int i = 0; i < totalPlayers; i++) {
		assert(g_rakPeerInterface->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, m_playerGUID[i], false));
	}

	ServerFindNextPlayerTurn();
}

//server read packets
void ServerReceiveNewPlayer(RakNet::Packet* packet) {
	RakNet::RakNetGUID guid = packet->guid;
	//find my player
	bool found = false;
	int index = 0;
	for (int i = 0; i < totalPlayers; i++) {
		if (m_playerGUID[i] == guid) {
			found = true;
			index = i;
			break;
		}
	}
	if (!found)
		return;

	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString userName;
	bs.Read(userName);
	RakNet::RakString userClass;
	bs.Read(userClass);
	int userDamage;
	bs.Read(userDamage);
	int userHeal;
	bs.Read(userHeal);
	int userHealth;
	bs.Read(userHealth);


	Player &player = m_player[index]; //get player info from map
	player.SetName(userName);
	player.charClass = userClass;
	player.damage = userDamage;
	player.heal = userHeal;
	player.SetMaxHealth(userHealth);
	player.ready = true;
	std::cout << "[" << player.name.c_str() << "|" << player.charClass.c_str() << "] is ready!" << std::endl;

	ServerShowClientStats(m_playerGUID[index]);
	//tell others

	if (ServerCheckPlayerCount())
		ServerReadyPlayers();
}

void ServerOnConnectionLost(RakNet::Packet* packet) {
	//need to let everyone else know who was gone
	//& remove from player

	RakNet::RakNetGUID guid = packet->guid;
	int index = 0;
	for (int i = 0; i < totalPlayers; i++) {
		if (m_playerGUID[i] == guid) {
			index = i;
			break;
		}
	}
	std::cout << m_player[index].name << "has disconnected." << std::endl;
	//INFORM other people that we removed ppl

	for (int i = 0; i < totalPlayers; i++) {
		if (i == index)
			continue;
		RakNet::BitStream bs;
		bs.Write((RakNet::MessageID)ID_TOCLIENT_PEERCONNECTIONLOST);
		bs.Write(m_player[i].name.c_str());
	}
	//pop from vector
	
	m_player.erase(m_player.begin() + index);
	m_playerGUID.erase(m_playerGUID.begin() + index);

	totalPlayers--;
	activePlayers--;
	ServerFindNextPlayerTurn();
}

void ServerProcessMove(RakNet::Packet* packet) {
	//packet info = id, targetname

	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString targetName;
	bs.Read(targetName);
	MoveType moveType;
	bs.Read((MoveType)moveType);


	RakNet::RakNetGUID attackerGuid = packet->guid;
	int attacker = -1; 
	bool found = false;
	for (int i = 0; i < totalPlayers; i++) {
		if (m_playerGUID[i] == attackerGuid) {
			found = true;
			attacker = i;
			break;
		}
	}


	int defender = -1;
	bool foundDefender = false;
	for (int i = 0; i < totalPlayers; i++) {
		if ((RakNet::RakString)m_player[i].name.c_str() == targetName.C_String() && m_player[i].health > 0 
			&& targetName.C_String() != m_player[attacker].name) {
			foundDefender = true;
			defender = i;
			break;
		}
	}

	if (!foundDefender || !found) {
		//failed to find either the player and ourselves
		RakNet::BitStream ToClientAttacker;
		ToClientAttacker.Write((RakNet::MessageID)ID_TOCLIENT_INVALIDNAME);
		ToClientAttacker.Write(targetName.C_String());
		assert(g_rakPeerInterface->Send(&ToClientAttacker, HIGH_PRIORITY, RELIABLE_ORDERED, 0, attackerGuid, false));
		return;
	}

	else {
		//packet info = ID, enemyName, damage, myCurrentHealth
		if(moveType == Attack)
			m_player[defender].TakeDamage(m_player[attacker].name, m_player[attacker].damage);
		else
			m_player[defender].HealedBy(m_player[attacker].name, m_player[attacker].heal);

		RakNet::BitStream ToClientAttacker;
		if (moveType == Attack)
			ToClientAttacker.Write((RakNet::MessageID)ID_TOCLIENT_ATTACKTARGET);
		else
			ToClientAttacker.Write((RakNet::MessageID)ID_TOCLIENT_HEALTARGET);
		ToClientAttacker.Write(m_player[defender].name.c_str());
		if(moveType == Attack)
			ToClientAttacker.Write(m_player[attacker].damage);
		else
			ToClientAttacker.Write(m_player[attacker].heal);
		ToClientAttacker.Write(m_player[defender].health);
		ToClientAttacker.Write(m_player[defender].maxHealth);
		assert(g_rakPeerInterface->Send(&ToClientAttacker, HIGH_PRIORITY, RELIABLE_ORDERED, 0, m_playerGUID[attacker], false));

		RakNet::BitStream ToClientDefender;
		if (moveType == Attack)
			ToClientDefender.Write((RakNet::MessageID)ID_TOCLIENT_TAKEDAMAGE);
		else
			ToClientDefender.Write((RakNet::MessageID)ID_TOCLIENT_HEALEDBY);
		ToClientDefender.Write(m_player[attacker].name.c_str());
		if(moveType == Attack)
			ToClientDefender.Write(m_player[attacker].damage);
		else
			ToClientDefender.Write(m_player[attacker].heal);
		ToClientDefender.Write(m_player[defender].health);
		ToClientDefender.Write(m_player[defender].maxHealth);
		assert(g_rakPeerInterface->Send(&ToClientDefender, HIGH_PRIORITY, RELIABLE_ORDERED, 0, m_playerGUID[defender], false));

		RakNet::BitStream ToClientBystander;
		//packet info: Message ID, attackerName, defenderName, moveType, attackerDamage, attackerHeal, defenderHealth, defenderMaxHealth
		ToClientBystander.Write((RakNet::MessageID)ID_TOCLIENT_WATCHCOMBAT);
		ToClientBystander.Write(m_player[attacker].name.c_str());
		ToClientBystander.Write(m_player[defender].name.c_str());
		ToClientBystander.Write((int)moveType);
		ToClientBystander.Write(m_player[attacker].damage);
		ToClientBystander.Write(m_player[attacker].heal);
		ToClientBystander.Write(m_player[defender].health);
		ToClientBystander.Write(m_player[defender].maxHealth);
		for (int i = 0; i < totalPlayers; i++) 
			if (i != attacker && i != defender)
				assert(g_rakPeerInterface->Send(&ToClientBystander, HIGH_PRIORITY, RELIABLE_ORDERED, 0, m_playerGUID[i], false));


		ServerFindNextPlayerTurn();
	}
}

void ServerProcessPlayerDeath(RakNet::Packet* packet) {
	activePlayers = activePlayers - 1;

	if (!ServerPlayersStillAlive()) {
		ServerEndGame();
		return;
	}

	RakNet::RakNetGUID deadPlayerGUID = packet->guid;

	int deadPlayerIndex = 0;
	bool found = false;
	for (int i = 0; i < totalPlayers; i++) {
		if (m_playerGUID[i] == deadPlayerGUID) {
			found = true;
			deadPlayerIndex = i;
			break;
		}
	}
	if (!found)
		return;

	//write packet info: id, deadplayername, remainingplayers
	RakNet::BitStream bs;
	bs.Write((RakNet::MessageID)ID_TOCLIENT_INFORMPLAYERDEATH);
	bs.Write(m_player[deadPlayerIndex].name.c_str());
	bs.Write(activePlayers);
	for (int i = 0; i < totalPlayers; i++) {
		if (i != deadPlayerIndex)
			assert(g_rakPeerInterface->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, m_playerGUID[i], false));
	}
}

void ServerClearClientScreen() {
	RakNet::BitStream bs;
	bs.Write((RakNet::MessageID)ID_TOCLIENT_CLEARSCREEN);
	for (int i = 0; i < totalPlayers; i++) 
		assert(g_rakPeerInterface->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, m_playerGUID[i], false));
}
void ServerClearClientLine() {
	RakNet::BitStream bs;
	bs.Write((RakNet::MessageID)ID_TOCLIENT_CLEARLINE);
	bs.Write((int)2);
	for (int i = 0; i < totalPlayers; i++)
		assert(g_rakPeerInterface->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, m_playerGUID[i], false));


}

//shown only to our specified player
void ServerShowClientStats(RakNet::RakNetGUID playerGUID) {
	int index = 0;
	for (int i = 0; i < totalPlayers; i++) {
		if (m_playerGUID[i] == playerGUID) {
			index = i;
			break;
		}
	}
	RakNet::BitStream bs;
	bs.Write((RakNet::MessageID)ID_TOCLIENT_SHOWSTATS);
	bs.Write(m_player[index].name.c_str());
	bs.Write(m_player[index].charClass.c_str());
	bs.Write(m_player[index].damage);
	bs.Write(m_player[index].heal);
	bs.Write(m_player[index].health);
	bs.Write(m_player[index].maxHealth);
	assert(g_rakPeerInterface->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, m_playerGUID[index], false));


}

//sent  to all players,shows their playerstat
void ServerShowClientStats() {
	for(int i = 0; i < totalPlayers; i++){
		//packet info: MessageID, CurrentPlayer's Name, Class, Damage, Health and MaxHealth
		RakNet::BitStream bs;
		bs.Write((RakNet::MessageID)ID_TOCLIENT_SHOWSTATS);
		bs.Write(m_player[i].name.c_str());
		bs.Write(m_player[i].charClass.c_str());
		bs.Write(m_player[i].damage);
		bs.Write(m_player[i].heal);
		bs.Write(m_player[i].health);
		bs.Write(m_player[i].maxHealth);
		assert(g_rakPeerInterface->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, m_playerGUID[i], false));
	}
}


void ServerShowOtherClientStats(RakNet::Packet* packet) {
	RakNet::RakNetGUID guid = packet->guid;

	int playerIndex = -1; //which info to exclude
	for (int i = 0; i < totalPlayers; i++) {
		if (m_playerGUID[i] == guid) {
			playerIndex = i;
			break;
		}
	}
	//packet info: messageid, #ofPpl, write num stuff
	RakNet::BitStream bs;
	bs.Write((RakNet::MessageID)ID_TOCLIENT_SHOWOTHERSTATS);
	bs.Write(totalPlayers - 1);
	for (int i = 0; i < totalPlayers; i++) {
		if (i == playerIndex)
			continue;
		//packetinfo: id, numloop, name, char, damage, heal, health, maxhealth
		bs.Write(m_player[i].name.c_str());
		bs.Write(m_player[i].charClass.c_str());
		bs.Write(m_player[i].damage);
		bs.Write(m_player[i].heal);
		bs.Write(m_player[i].health);
		bs.Write(m_player[i].maxHealth);
	}
	assert(g_rakPeerInterface->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, m_playerGUID[playerIndex], false));

}

//client packets
void ClientOnConnectionAccepted(RakNet::Packet* packet) {
	//server should not be connecting to anybody
	//only clients should connect  to server
	assert(!isServer);

	g_networkState = NS_Lobby;
	//now we can send packets to the server at any time with this global var
	g_serverAddress = packet->systemAddress;

}

void ClientSetPlayerReady(RakNet::Packet* packet) {
	assert(!isServer);
	g_networkState = NS_Ready;
}

void ClientSetPlayerWaiting(RakNet::Packet* packet) {
	assert(!isServer);


	//packet info: MessageID, CurrentPlayer's Name, Class, Damage, Health and MaxHealth
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString currPlayerName;
	bs.Read(currPlayerName);
	RakNet::RakString currPlayerClass;
	bs.Read(currPlayerClass);
	int currPlayerDamage;
	bs.Read(currPlayerDamage);
	int currPlayerHeal;
	bs.Read(currPlayerHeal);
	int currPlayerHealth;
	bs.Read(currPlayerHealth);
	int currPlayerMaxHealth;
	bs.Read(currPlayerMaxHealth);

	std::cout << "Current Turn: [" << currPlayerName.C_String() << "|" << currPlayerClass <<
		"] - ATK: " << currPlayerDamage << " - HEAL: " << currPlayerHeal << " - HP: " << currPlayerHealth << "/" << currPlayerMaxHealth << std::endl;

	g_networkState = NS_Waiting;
}

void ClientSetPlayerStartTurn(RakNet::Packet* packet) {
	//packet info= id, numPlayers, player1, player2...
	//Lists the targets
	assert(!isServer);

	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	int playerNum;
	bs.Read(playerNum);
	std::cout << "It's your turn!" << std::endl
			<< "Other Players: ";
	for (int i = 0; i < playerNum-1; i++) {
		RakNet::RakString playerName;
		bs.Read(playerName);
		std::cout << "["<< playerName.C_String() << "]";
	}
	std::cout << std::endl;
	std::cout << "Attacking (a, attack, attacking) or Healing (h, heal, healing)." << std::endl;
	g_networkState = NS_PlayerTurn;
}

void ClientPlayerInvalidName(RakNet::Packet* packet) {
	assert(!isServer);
	//packet info = id, invalid name
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString failedName;
	bs.Read(failedName);
	std::cout << failedName.C_String() << " is not a valid target. Please select again" << std::endl;
	g_networkState = NS_PlayerTurn;
}

void ClientPlayerAttackTarget(RakNet::Packet* packet) {
	assert(!isServer);
	//packet info = ID, mytarget, my damage, their current health
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString targetName;
	bs.Read(targetName);
	int damage;
	bs.Read(damage);
	int enemyHealth;
	bs.Read(enemyHealth);
	int enemyMaxHealth;
	bs.Read(enemyMaxHealth);

	std::cout << "You hit " << targetName.C_String() << " for " << damage << "!" << std::endl
		<< targetName.C_String() << "'s Health: " << enemyHealth << "/" << enemyMaxHealth << std::endl;
	g_networkState = NS_Waiting;
}

void ClientPlayerHealTarget(RakNet::Packet* packet) {
	assert(!isServer);
	//packet info = ID, mytarget, my heal, their current health
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString targetName;
	bs.Read(targetName);
	int heal;
	bs.Read(heal);
	int enemyHealth;
	bs.Read(enemyHealth);
	int enemyMaxHealth;
	bs.Read(enemyMaxHealth);

	std::cout << "You healed " << targetName.C_String() << " for " << heal << "!" << std::endl
		<< targetName.C_String() << "'s Health: " << enemyHealth << "/" << enemyMaxHealth << std::endl;
	g_networkState = NS_Waiting;
}

void ClientPlayerTookDamage(RakNet::Packet* packet) {
	assert(!isServer);
	//packet info = ID, name, damage, myCurrentHealth
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString targetName;
	bs.Read(targetName);
	int incomingValue;
	bs.Read(incomingValue);
	int currentHealth;
	bs.Read(currentHealth);
	int maxHealth;
	bs.Read(maxHealth);

	std::cout << targetName << " hit you for " << incomingValue << "!" << std::endl 
		<< "Current Health: " <<currentHealth << "/" << maxHealth <<std::endl;

	if (currentHealth <= 0) {
		std::cout << "You have been defeated!" << std::endl;
		RakNet::BitStream NotifyServerOfDeath;
		//packet info: Id, index
		NotifyServerOfDeath.Write((RakNet::MessageID)ID_TOSERVER_PLAYERDEATH);
		assert(g_rakPeerInterface->Send(&NotifyServerOfDeath, HIGH_PRIORITY, RELIABLE_ORDERED, 0, g_serverAddress, false));
	}

}

void ClientPlayerHealedBy(RakNet::Packet* packet) {
	assert(!isServer);
	//packet info = ID, name, damage, myCurrentHealth
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString targetName;
	bs.Read(targetName);
	int incomingValue;
	bs.Read(incomingValue);
	int currentHealth;
	bs.Read(currentHealth);
	int maxHealth;
	bs.Read(maxHealth);

	std::cout << targetName << " healed you for " << incomingValue << "!" << std::endl
		<< "Current Health: " << currentHealth << "/" << maxHealth << std::endl;
}



void ClientPlayerWatchCombat(RakNet::Packet* packet) {//packet info: Message ID, attackerName, defenderName, moveType, attackerDamage, attackerHeal, defenderHealth, defenderMaxHealth
		
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString attackerName;
	bs.Read(attackerName);
	RakNet::RakString defenderName;
	bs.Read(defenderName);
	int moveType;
	bs.Read(moveType);
	int attackerDamage;
	bs.Read(attackerDamage);
	int attackerHeal;
	bs.Read(attackerHeal);
	int defenderCurrentHealth;
	bs.Read(defenderCurrentHealth);
	int defenderMaxHealth;
	bs.Read(defenderMaxHealth);
	if((MoveType)moveType == Attack)
		std::cout << attackerName << " attacked " << defenderName << " for " << attackerDamage << "! " << std::endl;
	else
		std::cout << attackerName << " healed " << defenderName << " for " << attackerHeal << "! " << std::endl;

	std::cout << defenderName << "'s health is at " << defenderCurrentHealth << "/" << defenderMaxHealth << std::endl;
	//player1 attacked player2 for x damage! Player2's health is at 20/100
}

void ClientPlayerInformDeath(RakNet::Packet* packet) {
	assert(!isServer);
	//packet info: id, deadplayername, remainingplayers
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString deadPlayerName;
	bs.Read(deadPlayerName);
	int remainingPlayers;
	bs.Read(remainingPlayers);
	std::cout << deadPlayerName.C_String() << " has been killed! Only " << remainingPlayers << " remain!" << std::endl;
}

void ClientClearScreen(RakNet::Packet* packet) {
	system("CLS");
}

void ClientClearLine(RakNet::Packet* packet) {
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	int num;
	bs.Read(num);
	for (int i = 0; i < num; i++)
	{
		std::cout << std::endl;
	}
}

void ClientShowPlayerStats(RakNet::Packet* packet) {
	//packet info: ID, name,  class, damage, health and maxhealth
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString name;
	bs.Read(name);
	RakNet::RakString charClass;
	bs.Read(charClass);
	int damage;
	bs.Read(damage);
	int heal;
	bs.Read(heal);
	int health;
	bs.Read(health);
	int maxHealth;
	bs.Read(maxHealth);
		std::cout << ">>>> My Character: [" << name.C_String() << "|" << charClass <<
		"] - ATK: " << damage << " - HEAL: " << heal << " - HP: " << health << "/" << maxHealth << std::endl 
			<< "Type stats/Stats/STATS at any time to view other players" << std::endl << std::endl;


}


void ClientRequestOtherStats() {
	RakNet::BitStream bs;
	bs.Write((RakNet::MessageID)ID_TOSERVER_REQUESTOTHERSTATS);
	assert(g_rakPeerInterface->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, g_serverAddress, false));

}


void ClientShowOtherPlayerStats(RakNet::Packet* packet) {
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	int numPpl;
	bs.Read(numPpl);
	for (int i = 0; i < numPpl; i++) {
		RakNet::RakString name;
		bs.Read(name);
		RakNet::RakString charClass;
		bs.Read(charClass);
		int damage;
		bs.Read(damage);
		int heal;
		bs.Read(heal);
		int health;
		bs.Read(health);
		int maxHealth;
		bs.Read(maxHealth);
		std::cout << ">> [" << name.C_String() << "|" << charClass <<
			"] - ATK: " << damage << " - HEAL: " << heal << " - HP: " << health << "/" << maxHealth << std::endl;

	}
}


void ClientPlayerCharacterDeath(RakNet::Packet* packet) {
	assert(!isServer);
	std::cout << "You have been defeated!" << std::endl;
	g_networkState = NS_End;
}


void ClientEndGame(RakNet::Packet* packet) {
	//packet info: id, victor name
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString name;
	bs.Read(name);

	std::cout << "The game has ended! " << name << " is victorious!" << std::endl;
	isRunning = false;
	g_networkState = NS_End;
}

void ClientOnPeerConnectionLost(RakNet::Packet* packet) {
	//packet info: id, name lost;
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString name;
	bs.Read(name);

	std::cout << name << " has disconnected from the game." << std::endl;
}

unsigned char GetPacketIdentifier(RakNet::Packet *packet)
{
	if (packet == nullptr)
		return 255;

	if ((unsigned char)packet->data[0] == ID_TIMESTAMP)
	{
		RakAssert(packet->length > sizeof(RakNet::MessageID) + sizeof(RakNet::Time));
		return (unsigned char)packet->data[sizeof(RakNet::MessageID) + sizeof(RakNet::Time)];
	}
	else
		return (unsigned char)packet->data[0];
}


void InputHandler()
{
	while (isRunning)
	{
		std::string userInput;
		if (g_networkState == NS_Init)
		{
			std::cout << "press [s] to start a server. Press any key for client" << std::endl;
			std::cout << ">"; std::cin >> userInput;
			isServer = (userInput[0] == 's');
			g_networkState = NS_PendingStart;
		}

		else if (g_networkState == NS_Lobby) 
		{
			std::cout << "Enter your name to play or type quit to leave" << std::endl;
			bool choseName = false;
			while (!choseName) {
				std::cout << ">"; std::cin >> userInput;
				if (userInput == "stats" || userInput == "Stats" || userInput == "STATS") {
					std::cout << "Invalid name: Cannot use the name Stats'" << std::endl;
				} 
				else if (userInput == "quit" || userInput == "Quit" || userInput == "QUIT") {
					std::cout << "Quitting game" << std::endl;
					isRunning = false;
					return;
				}
				else {
					choseName = true;
				}
			}



			RakNet::RakString name = userInput.c_str();
			RakNet::RakString charClass;
			int damage;
			int heal;
			int health;

			std::cout << "Choose your class!" << std::endl
				<< "Warrior (w, war, warrior) - ATK: 10 - HEAL: 10 - HP: 100" << std::endl
				<< "Mage    (m, mag, mage)    - ATK: 20 - HEAL: 10 - HP: 70" << std::endl
				<< "Beast   (b, bea, beast)   - ATK: 5  - HEAL: 5  - HP: 150" << std::endl;

			bool classSelected = false;
			while (!classSelected) {
				std::cout << ">"; std::cin >> userInput;
				if(userInput == "w" || userInput == "war" || userInput == "warrior"){
					charClass = "Warrior";
					damage = 10;
					heal = 10;
					health = 100;
					classSelected = true;
				}
				else if (userInput == "m" || userInput == "mag" || userInput == "mage") {
					charClass = "Mage";
					damage = 20;
					heal = 15;
					health = 70;
					classSelected = true;
				}
				else if (userInput == "b" || userInput == "bea" || userInput == "beast") {
					charClass = "Beast";
					damage = 5;
					heal = 5;
					health = 150;
					classSelected = true;
				}
				else {
					std::cout << "Invalid entry! Choose your class!" << std::endl;
				}
			}

			RakNet::BitStream bs;
			bs.Write((RakNet::MessageID)ID_TOSERVER_LOBBY_READY);
			bs.Write(name);
			bs.Write(charClass);
			bs.Write(damage);
			bs.Write(heal);
			bs.Write(health);

			//returns 0 if something bad happens, crash if so
			assert(g_rakPeerInterface->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, g_serverAddress, false));
			//wait for server to acknowledge this, we need to switch to another state

			RakNet::BitStream requestInfo;

			g_networkState = NS_Pending;
			//system("CLS");
		}
		else if (g_networkState == NS_Pending) {
			//Waiting for lobby ready
			static bool  pendingMessage = false;
			if (!pendingMessage) {
				std::cout << "Waiting for other players..."	<< std::endl;
				pendingMessage = true;
			}
		}
		else if (g_networkState == NS_Ready) {
			//game has started, still determining states

		}
		else if (g_networkState == NS_Waiting || g_networkState == NS_PlayerTurn) {

			//not your turn
			std::cout << ">"; std::cin >> userInput;

			if (g_networkState == NS_Waiting) {
				if (userInput == "STATS" || userInput == "Stats" || userInput == "stats")
					ClientRequestOtherStats();
			}
			else if (g_networkState == NS_PlayerTurn) {			//your turn
				MoveType moveType;
				bool choseMoveType = false;
				while (!choseMoveType) {
					if (userInput == "a" || userInput == "attack" || userInput == "attacking") {
						choseMoveType = true;
						moveType = Attack;
					}
					else if (userInput == "h" || userInput == "heal" || userInput == "healing") {
						choseMoveType = true;
						moveType = Heal;
					}
					else if (userInput == "STATS" || userInput == "Stats" || userInput == "stats") {
						ClientRequestOtherStats();
					}
					else {
						std::cout << "Invalid Choice: Choose to attack or heal first!" << std::endl;
						std::cout << ">"; std::cin >> userInput;
					}
				}
				std::cout << "Enter the name of a player you want to target!" << std::endl;
				bool choseTarget = false;
				while (!choseTarget) {
					std::cout << ">"; std::cin >> userInput;
					if (userInput == "STATS" || userInput == "Stats" || userInput == "stats") {
						ClientRequestOtherStats();
					}
					else {
						choseTarget = true;
					}
				}

				RakNet::BitStream bs;
				bs.Write((RakNet::MessageID)ID_TOSERVER_PLAYERMOVE);
				RakNet::RakString name(userInput.c_str());
				bs.Write(name);
				bs.Write((int)moveType);

				//returns 0 if something bad happens, crash if so
				assert(g_rakPeerInterface->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, g_serverAddress, false));
				g_networkState = NS_Pending;
			}
		}
		std::this_thread::sleep_for(std::chrono::microseconds(100));
	}	
}


//Check if this is something that is low level
bool HandleLowLevelPackets(RakNet::Packet* packet) {

	bool isHandled = true;

	// We got a packet, get the identifier with our handy function
	unsigned char packetIdentifier = GetPacketIdentifier(packet);

	// Check if this is a network message packet
	switch (packetIdentifier)
	{
	case ID_DISCONNECTION_NOTIFICATION:
		// Connection lost normally
		ServerOnConnectionLost(packet);
		printf("ID_DISCONNECTION_NOTIFICATION\n");
		break;
	case ID_ALREADY_CONNECTED:
		// Connection lost normally
		printf("ID_ALREADY_CONNECTED with guid %" PRINTF_64_BIT_MODIFIER "u\n", packet->guid);
		break;
	case ID_INCOMPATIBLE_PROTOCOL_VERSION:
		printf("ID_INCOMPATIBLE_PROTOCOL_VERSION\n");
		break;
	case ID_REMOTE_DISCONNECTION_NOTIFICATION: // Server telling the clients of another client disconnecting gracefully.  You can manually broadcast this in a peer to peer enviroment if you want.
		printf("ID_REMOTE_DISCONNECTION_NOTIFICATION\n");
		break;
	case ID_REMOTE_CONNECTION_LOST: // Server telling the clients of another client disconnecting forcefully.  You can manually broadcast this in a peer to peer enviroment if you want.
		printf("ID_REMOTE_CONNECTION_LOST\n");
		break;
	case ID_NEW_INCOMING_CONNECTION: // Server telling the clients of another client connecting.  You can manually broadcast this in a peer to peer enviroment if you want.
		ServerOnIncomingConnection(packet);
		printf("ID_NEW_INCOMING_CONNECTION\n");
		break;
	case ID_REMOTE_NEW_INCOMING_CONNECTION: // Server telling the clients of another client connecting.  You can manually broadcast this in a peer to peer enviroment if you want.
		ServerOnIncomingConnection(packet);
		printf("ID_REMOTE_NEW_INCOMING_CONNECTION\n");
		break;
	case ID_CONNECTION_BANNED: // Banned from this server
		printf("We are banned from this server.\n");
		break;
	case ID_CONNECTION_ATTEMPT_FAILED:
		printf("Connection attempt failed\n");
		break;
	case ID_NO_FREE_INCOMING_CONNECTIONS:
		// Sorry, the server is full.  I don't do anything here but
		// A real app should tell the user
		printf("ID_NO_FREE_INCOMING_CONNECTIONS\n");
		break;

	case ID_INVALID_PASSWORD:
		printf("ID_INVALID_PASSWORD\n");
		break;

	case ID_CONNECTION_LOST:
		// Couldn't deliver a reliable packet - i.e. the other system was abnormally
		// terminated
		ServerOnConnectionLost(packet);
		printf("ID_CONNECTION_LOST\n");
		break;

	case ID_CONNECTION_REQUEST_ACCEPTED:
		// This tells the client they have connected
		printf("ID_CONNECTION_REQUEST_ACCEPTED to %s with GUID %s\n", packet->systemAddress.ToString(true), packet->guid.ToString());
		printf("My external address is %s\n", g_rakPeerInterface->GetExternalID(packet->systemAddress).ToString(true));
		ClientOnConnectionAccepted(packet);

		break;
	case ID_CONNECTED_PING:
	case ID_UNCONNECTED_PING:
		printf("Ping from %s\n", packet->systemAddress.ToString(true));
		break;
	default:
		isHandled = false;
		break;
	}

	return isHandled;
}

void PacketHandler()
{
	while (isRunning)
	{
		for (RakNet::Packet* packet = g_rakPeerInterface->Receive(); packet != nullptr; g_rakPeerInterface->DeallocatePacket(packet), packet = g_rakPeerInterface->Receive())
		{
			if (!HandleLowLevelPackets(packet)) {
				//wasn't handled, handle it by ourself
				//our game specific packets
				unsigned char packetIdentifier = GetPacketIdentifier(packet);
				switch (packetIdentifier)
				{
				case ID_TOSERVER_LOBBY_READY:
					ServerReceiveNewPlayer(packet);
					break;
				case ID_TOSERVER_PLAYERMOVE:
					ServerProcessMove(packet);
					break;
				case ID_TOSERVER_PLAYERDEATH:
					ServerProcessPlayerDeath(packet);
					break;
				case ID_TOSERVER_REQUESTOTHERSTATS:
					ServerShowOtherClientStats(packet);
					break;


				case ID_TOCLIENT_READY:
					ClientSetPlayerReady(packet);
					break;
				case ID_TOCLIENT_WAITING:
					ClientSetPlayerWaiting(packet);
					break;
				case ID_TOCLIENT_STARTTURN:
					ClientSetPlayerStartTurn(packet);
					break;
				case ID_TOCLIENT_INVALIDNAME:
					ClientPlayerInvalidName(packet);
					break;
				case ID_TOCLIENT_ATTACKTARGET:
					ClientPlayerAttackTarget(packet);
					break;
				case ID_TOCLIENT_HEALTARGET:
					ClientPlayerHealTarget(packet);
					break;

				case ID_TOCLIENT_TAKEDAMAGE:
					ClientPlayerTookDamage(packet);
					break;
				case ID_TOCLIENT_HEALEDBY:
					ClientPlayerHealedBy(packet);
					break;
				case ID_TOCLIENT_WATCHCOMBAT:
					ClientPlayerWatchCombat(packet);
					break;
				case ID_TOCLIENT_INFORMPLAYERDEATH:
					ClientPlayerInformDeath(packet);
					break;

				case ID_TOCLIENT_CLEARSCREEN:
					ClientClearScreen(packet);
					break;
				case ID_TOCLIENT_CLEARLINE:
					ClientClearLine(packet);
					break;
				case ID_TOCLIENT_SHOWSTATS:
					ClientShowPlayerStats(packet);
					break;
				case ID_TOCLIENT_SHOWOTHERSTATS:
					ClientShowOtherPlayerStats(packet);
					break;
				case ID_TOCLIENT_CHARACTERDEATH:
					ClientPlayerCharacterDeath(packet);
					break;

				case ID_TOCLIENT_ENDGAME:
					ClientEndGame(packet);
					break;
				case ID_TOCLIENT_PEERCONNECTIONLOST:
					ClientOnPeerConnectionLost(packet);
					break;
				default:
					break;
				}
			}
		}

			
		std::this_thread::sleep_for(std::chrono::microseconds(100));
	}
}




int main()
{
	g_rakPeerInterface = RakNet::RakPeerInterface::GetInstance();

	std::thread inputHandler(InputHandler);
	std::thread packetHandler(PacketHandler);

	while (isRunning)
	{
		//Setting up for server or client
		if (g_networkState == NS_PendingStart)
		{
			if (isServer)
			{
				RakNet::SocketDescriptor socketDescriptors[1];
				socketDescriptors[0].port = SERVER_PORT;
				socketDescriptors[0].socketFamily = AF_INET; // Test out IPV4

				bool isSuccess = g_rakPeerInterface->Startup(MAX_CONNECTIONS, socketDescriptors, 1) == RakNet::RAKNET_STARTED;
				assert(isSuccess);
				//ensures we are server
				g_rakPeerInterface->SetMaximumIncomingConnections(MAX_CONNECTIONS);
				std::cout << "Server started" << std::endl;
				g_networkState = NS_Started;
				g_serverState = SS_Waiting;
			}
			//client
			else
			{
				RakNet::SocketDescriptor socketDescriptor(CLIENT_PORT, 0);
				socketDescriptor.socketFamily = AF_INET;
				
				while (RakNet::IRNS2_Berkley::IsPortInUse(socketDescriptor.port, socketDescriptor.hostAddress, socketDescriptor.socketFamily, SOCK_DGRAM))
					socketDescriptor.port++;
				
				RakNet::StartupResult result = g_rakPeerInterface->Startup(8, &socketDescriptor, 1);
				assert(result == RakNet::RAKNET_STARTED);
				g_rakPeerInterface->SetOccasionalPing(true);
				//"127.0.0.1" = local host = your machines address
				RakNet::ConnectionAttemptResult car = g_rakPeerInterface->Connect("127.0.0.1", SERVER_PORT, nullptr, 0);
				RakAssert(car == RakNet::CONNECTION_ATTEMPT_STARTED);
				std::cout << "Client attempted connection..." << std::endl;
				g_networkState = NS_Started;
			}
		}


		
	}

	//std::cout << "press q and then return to exit" << std::endl;
	//std::cout << ">"; std::cin >> userInput;

	inputHandler.join();
	packetHandler.join();
	system("pause");
	return 0;
}