#include "FUNCTIONS.h"
#include <vector>
#include <iostream>
#include <algorithm>

using namespace std;


	vector<double> FUNCTIONS::short_to_double(vector<short> &data)
	{
		std::vector<double> audio_double(data.size());
		std::transform(data.begin(), data.end(), audio_double.begin(), [](short val) {
			return static_cast<double>(val);
			});
		return audio_double;
	}

	vector<short> FUNCTIONS::double_to_short(vector<double> &data)
	{
		return vector<short>();
	}

	std::pair<vector<short>, vector<short>> FUNCTIONS::split_audio(vector<short> &data)
	{
		return std::pair<vector<short>, vector<short>>();
	}

	vector<short> FUNCTIONS::consolidate(vector<short> &left, vector<short> &right)
	{
		return vector<short>();
	}



