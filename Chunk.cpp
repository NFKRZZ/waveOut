#include "Chunk.h"
#include "GLOBAL.h"
#include <vector>
#include <iostream>
void Chunk::clampKeys()//HI
{
	//std::cout << "Clamp Keys Called"<<std::endl;
		Key k = GLOBAL::MUSICAL_KEY;
		std::vector<Keys> ref;
		switch (k)
		{
		case Key::A_MAJOR:
			ref = GLOBAL::aMajor;
			break;
		case Key::A_SHARP_MAJOR:
			ref = GLOBAL::aSharpMajor;
			break;
		case Key::B_MAJOR:
			ref = GLOBAL::bMajor;
			break;
		case Key::C_MAJOR:
			ref = GLOBAL::cMajor;
			break;
		case Key::C_SHARP_MAJOR:
			ref = GLOBAL::cSharpMajor;
			break;
		case Key::D_MAJOR:
			ref = GLOBAL::dMajor;
			break;
		case Key::D_SHARP_MAJOR:
			ref = GLOBAL::dSharpMajor;
			break;
		case Key::E_MAJOR:
			ref = GLOBAL::eMajor;
			break;
		case Key::F_MAJOR:
			ref = GLOBAL::fMajor;
			break;
		case Key::F_SHARP_MAJOR:
			ref = GLOBAL::fSharpMajor;
			break;
		case Key::G_MAJOR:
			ref = GLOBAL::gMajor;
			break;
		case Key::G_SHARP_MAJOR:
			ref = GLOBAL::gSharpMajor;
			break;
		case Key::NO_KEY:
			break;
		}
		
		if (this->singular == false)
		{
			//std::cout << "Non Singular!"<<std::endl;
			std::vector<Keys> keyvec;

			for (double f : this->freqVec)
			{
				double k = 0;
				//std::cout << "THIS IS K: " << k <<" THIS IS SIZE OF freqVec: "<<freqVec.size()<<std::endl;


				double minDiff = std::abs(f - static_cast<double>(ref[0]));
				Keys closestKey = ref[0];
				//std::cout << "THIS IS SIZE OF REF: " << ref.size() << std::endl;
				for (size_t i = 1;i < ref.size();i++)
				{
					//std::cout << "THIS IS I: " << i << std::endl;
					double diff = std::abs(f - static_cast<double>(ref[i]));
					if (diff < minDiff)
					{
						minDiff = diff;
						closestKey = ref[i];
					}
				}
				keyvec.push_back(closestKey);
				k++;
			}
			this->keyVec = keyvec;
			
		}
		else
		{
			//std::cout << "Singular!" << std::endl;
			double minDiff = std::abs(this->freq - static_cast<double>(ref[0]));
			Keys closestKey = ref[0];

			for (size_t i = 1;i < ref.size();i++)
			{
				double diff = std::abs(this->freq - static_cast<double>(ref[i]));
				if (diff < minDiff)
				{
					minDiff = diff;
					closestKey = ref[i];
				}
			}
			this->key = closestKey;
		}
		//std::cout << "FINI" << std::endl;
}
void Chunk::setTime()
{
	float startTime = this->chunkDuration * this->iter;

	float endTime = this->chunkDuration * this->iter + 1;

	this->startMinute = static_cast<int>(startTime / 60);
	this->startSecond = static_cast<int>(startTime) % 60;
	this->startMili = static_cast<int>(std::round((startTime - static_cast<float>(startSecond) - static_cast<float>(startMinute) * 60) * 1000));

	this->endMinute = static_cast<int>(endTime / 60);
	this->endSecond = static_cast<int>(endTime) % 60;
	this->endMili = static_cast<int>(std::round((endTime - static_cast<float>(endSecond) - static_cast<float>(endMinute) * 60) * 1000));
}
void Chunk::Init()
{
	this->clampKeys();
	this->setTime();
}