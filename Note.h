#pragma once

#include "Keys.h"

class Note
{
private:
	Key key;
	bool isStart;
	bool isEnd;

public:
	Note(Key key, bool isStart, bool isEnd)
	{
		this->key = key;
		this->isStart = isStart;
		this->isEnd = isEnd;
	}


	bool Start()
	{
		return isStart;
	}

	bool End()
	{
		return isEnd;
	}

	Key getKey()
	{
		return key;
	}

};