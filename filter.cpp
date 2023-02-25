#include <vector>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <numeric>
#include <complex>


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
	//-----------------------------------------------------------------------------------------------------------------------------------


	static std::vector<double> short_to_double(std::vector<short>& audio)
	{
		int audio_size = audio.size();
		std::vector<double> audio_double(audio_size);
		for (int i = 0; i < audio_size; i++)
		{
			audio_double[i] = (double)audio[i];
		}
		return audio_double;
	}






	//---------
	static std::vector<std::complex<double>> bilinear_transform(std::vector<std::complex<double>> a, std::vector<std::complex<double>> b, double Ts)
	{
		int N = a.size();
		int M = b.size();
		std::vector<std::complex<double>> a_discrete(N);
		std::vector<std::complex<double>> b_discrete(M);
		std::complex<double> z_inv;

		for (int i = 0; i < N; i++) {
			z_inv = std::pow(std::complex<double>(0, 1), -i);
			a_discrete[i] = (2 * Ts * a[i] + z_inv) / (2 * Ts * b[i] + z_inv);
		}

		for (int i = 0; i < M; i++) {
			z_inv = std::pow(std::complex<double>(0, 1), -i);
			b_discrete[i] = (2 * Ts * b[i]) / (2 * Ts * b[i] + z_inv);
		}

		return b_discrete;
	}


	/*static std::vector<double> lowpass_coefficients(double fc, double Ts)
	{
		double pi = 3.14159265358979323846;
		double K = tan(pi * fc * Ts);
		double norm = 1 / (1 + K);

		std::vector<double> coefs(5);
		coefs[0] = K * norm;
		coefs[1] = 2 * coefs[0];
		coefs[2] = coefs[0];
		coefs[3] = 2 * norm * (K - 1);
		coefs[4] = norm * (1 - K);
		return coefs;
	}*/


	static std::vector<long double> lowpass_coefficients(long double fc, long double Ts)
	{
		long double pi = 3.14159265358979323846;
		long double K = tan(pi * fc * Ts);
		long double norm = 1 / (1 + K);

		std::vector<long double> coefs(5);
		coefs[0] = K * norm;
		coefs[1] = 2 * coefs[0];
		coefs[2] = coefs[0];
		coefs[3] = 2 * norm * (K - 1);
		coefs[4] = norm * (1 - K);
		return coefs;
	}


	

	

};