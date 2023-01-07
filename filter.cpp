#include <vector>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <numeric>


using namespace std;
class filter
{
	
public:
	static void highPassFilter(pair<vector<double>,vector<double>> LeftRight, int cuttoffFrequency)
	{

	}


	//---------------------------------------------------------------------------------------------------

	static void yapply_high_pass_filter(std::vector<short>& left_channel, std::vector<short>& right_channel, const std::vector<double>& coefficients) {
		size_t num_taps = coefficients.size();

		// Apply the filter to the left channel.
		for (size_t i = 0; i < left_channel.size(); i++) {
			if (i + num_taps <= left_channel.size()) 
			{
				short filtered_sample = 0;

				for (size_t j = 0; j < num_taps; j++) 
				{
					filtered_sample += coefficients[j] * left_channel[i + j];
				}
				left_channel[i] = filtered_sample;
			}
			else 
			{
				left_channel[i] = 0;
			}
		}
		cout << "yesssa" << endl;
		// Apply the filter to the right channel.
		for (size_t i = 0; i < right_channel.size(); i++) 
		{
			if (i + num_taps <= right_channel.size()) 
			{
				short filtered_sample = 0;
				for (size_t j = 0; j < num_taps; j++) 
				{
					filtered_sample += coefficients[j] * right_channel[i + j];
				}
				right_channel[i] = filtered_sample;
			}
			else 
			{
				right_channel[i] = 0;
			}
		}
	}

	static vector<double> ycalculate_high_pass_filter_coefficients(double sample_rate, double cutoff_frequency, int num_taps) 
	{
		double M_PI = 3.1415926535897932384626433832795;
		double wc = 2.0 * M_PI * cutoff_frequency / sample_rate;
		std::vector<double> coefficients(num_taps, 0.0);
		//HAMMING WINDOW
		for (int i = 0; i < num_taps; i++) 
		{
			if (i == num_taps / 2) 
			{
				coefficients[i] = wc / M_PI;
			}
			else 
			{
				coefficients[i] = (sin(wc * (i - num_taps / 2))) / (M_PI * (i - num_taps / 2)) * (0.54 - 0.46 * cos(2.0 * M_PI * i / (num_taps - 1)));
			}
		}

		return coefficients;
	}

	static vector<long double> yLcalculate_high_pass_filter_coefficients(double sample_rate, double cutoff_frequency, int num_taps)
	{
		long double M_PI = 3.1415926535897932384626433832795L;
		long double wc = 2.0L * M_PI * cutoff_frequency / sample_rate;
		std::vector<long double> coefficients(num_taps, 0.0L);
		//HAMMING WINDOW
		for (int i = 0; i < num_taps; i++)
		{
			if (i == num_taps / 2)
			{
				coefficients[i] = wc / M_PI;
			}
			else
			{
				coefficients[i] = (sin(wc * (i - num_taps / 2))) / (M_PI * (i - num_taps / 2)) * (0.54 - 0.46 * cos(2.0L * M_PI * i / (num_taps - 1)));
			}
		}

		return coefficients;
	}
	// PUT TWO FUNCTIONS THAT USE LONG FOR MORE PRECISION
	static void yLapply_high_pass_filter(std::vector<short>& left_channel, std::vector<short>& right_channel, const std::vector<long double>& coefficients) {
		size_t num_taps = coefficients.size();

		// Apply the filter to the left channel.
		for (size_t i = 0; i < left_channel.size(); i++) {
			if (i + num_taps <= left_channel.size())
			{
				short filtered_sample = 0;

				for (size_t j = 0; j < num_taps; j++)
				{
					filtered_sample += coefficients[j] * left_channel[i + j];
				}
				left_channel[i] = filtered_sample;
			}
			else
			{
				left_channel[i] = 0;
			}
		}
		cout << "yesssa" << endl;
		// Apply the filter to the right channel.
		for (size_t i = 0; i < right_channel.size(); i++)
		{
			if (i + num_taps <= right_channel.size())
			{
				short filtered_sample = 0;
				for (size_t j = 0; j < num_taps; j++)
				{
					filtered_sample += coefficients[j] * right_channel[i + j];
				}
				right_channel[i] = filtered_sample;
			}
			else
			{
				right_channel[i] = 0;
			}
		}
	}



};