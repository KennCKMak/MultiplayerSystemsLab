#include <iostream>
#include <string>
#include "Player.h"


Player::Player() {
	name = "Unnamed";
	charClass = "Unknown";
	health = 100;
	maxHealth = 100;
	damage = 25;
	heal = 25;
	ready = false;
}




void Player::TakeDamage(std::string attackingPlayer, int num) {
	health -= num;
	if (health < 0)
		health = 0;
}

void Player::HealedBy(std::string attackingPlayer, int num) {
	health += num;
	if (health > maxHealth)
		health = maxHealth;
}
