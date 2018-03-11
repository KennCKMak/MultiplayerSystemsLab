#include "MessageIdentifiers.h"
#include "RakPeerInterface.h"
#include "BitStream.h"
#include <iostream>
#include <string>

class Player
{
public:
	Player();

	void Attack(std::string targetPlayer, int num);
	void TakeDamage(std::string attackingPlayer, int num);
	void HealedBy(std::string attackinkingPlayer, int num);
	void SetName(std::string newName) { name = newName; }
	void SetName(RakNet::RakString newName) { name = newName; }
	void SetMaxHealth(int num) { maxHealth = num; health = maxHealth; }



public:
	std::string name;
	std::string charClass;
	int health;
	int maxHealth;
	int damage;
	int heal;
	bool ready;
};