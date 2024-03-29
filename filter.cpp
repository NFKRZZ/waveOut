#include <vector>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <numeric>
#include <complex>
#include <fftw3.h>
#include "Util.h"
#include <math.h>
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
		vector<double> leftC;
		leftC.resize(left_channel.size());

		vector<double> rightC;
		rightC.resize(right_channel.size());


		// Apply the filter to the left channel.
		for (size_t i = 0; i < left_channel.size(); i++) {
			if (i + num_taps <= left_channel.size())
			{
				double filtered_sample = 0;

				for (size_t j = 0; j < num_taps; j++)
				{
					filtered_sample += coefficients[j] * left_channel[i + j];
				}
				leftC[i] = filtered_sample;
			}
			else
			{
				leftC[i] = 0;
			}
		}
		cout << "yesssa" << endl;
		// Apply the filter to the right channel.
		for (size_t i = 0; i < right_channel.size(); i++)
		{
			if (i + num_taps <= right_channel.size())
			{
				double filtered_sample = 0;
				for (size_t j = 0; j < num_taps; j++)
				{
					filtered_sample += coefficients[j] * right_channel[i + j];
				}
				rightC[i] = filtered_sample;
			}
			else
			{
				rightC[i] = 0;
			}
		}

		double maxValLeft = fabs(leftC[0]);
		double maxValRight = fabs(rightC[0]);

		for (size_t j = 1; j < leftC.size(); j++)
		{
			double absValLeft = fabs(leftC[j]);
			if (absValLeft > maxValLeft)
			{
				maxValLeft = absValLeft;
			}
		}

		for (size_t k = 1; k < rightC.size(); k++)
		{
			double absValRight = fabs(rightC[k]);
			if (absValRight > maxValRight)
			{
				maxValRight = absValRight;
			}
		}


		std::cout << "THIS IS MAX VALUE OF LEFT: " << maxValLeft << endl;
		std::cout << "THIS IS MAX VALUE OF RIGHT: " << maxValRight << endl;

		double maxi = (maxValLeft > maxValRight) ? maxValLeft : maxValRight;

		std::cout << "THIS IS MAXI: " << maxi << endl;

		double scaling_factor = maxi / (SHRT_MAX);

		for (size_t i = 0;i < left_channel.size();i++)
		{
			double hey = leftC[i] / scaling_factor;
			if (hey >= SHRT_MAX)
			{
				std::cout << "LEFT SIDE EXCEEDED MAX VALUE AT: " << i << " WITH VALUE: " << hey<<endl;
				hey--;
				
			}

			left_channel[i] = hey;
		}
		for (size_t i = 0;i < right_channel.size();i++)
		{
			double hey = rightC[i] / scaling_factor;
			if (hey >= SHRT_MAX)
			{
				std::cout << "RIGHT SIDE EXCEEDED MAX VALUE AT: " << i << " WITH VALUE: " << hey<<endl;
				hey--;
			}

			right_channel[i] = hey;
		}

		


	}
	//-----------------------------------------------------------------------------------------------------------------------------------

	static void yLapply_low_pass_filter(std::vector<short>& left_channel, std::vector<short>& right_channel, const std::vector<long double>& coefficients)
	{

	}


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

	static std::vector<long double> highpass_coefficients(long double fc, long double Ts)
	{

	}


	static void lowPassFFTW(std::vector<short>& left, std::vector<short>& right,int sampleRate, int cuttoff)
	{
		// Start with Left Side
		fftw_complex* in = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * left.size());
		fftw_complex* out = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * left.size());

		fftw_plan p = fftw_plan_dft_1d(left.size(), in, out, FFTW_FORWARD, FFTW_ESTIMATE);

		for (int i = 0; i < left.size(); i++)
		{
			in[i][0] = left[i]; //populate in with audio data
			in[i][1] = 0.0;
		}

		fftw_execute(p);

		double freq_step = (double)sampleRate / left.size();
		double cutoff_bin = cuttoff / freq_step;					// Modify frequency data past cutoff, make it zero
		for (int i = cutoff_bin; i < left.size() - cutoff_bin; i++)
		{
			out[i][0] = 0.0;
			out[i][1] = 0.0;
		}

		fftw_plan q = fftw_plan_dft_1d(left.size(), out, in, FFTW_BACKWARD, FFTW_ESTIMATE); //invert back to time domain
		fftw_execute(q);

		for (int i = 0; i < left.size(); i++)
		{
			left[i] = in[i][0] / left.size(); //modify amplitude
		}

		//Empty Mem
		fftw_destroy_plan(p);
		fftw_destroy_plan(q);
		fftw_free(in);
		fftw_free(out);


		//Now Do Right Side
		std::cout << "Did left" << std::endl;
		fftw_complex* inR = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * right.size());
		fftw_complex* outR = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * right.size());

		fftw_plan pR = fftw_plan_dft_1d(right.size(), inR, outR, FFTW_FORWARD, FFTW_ESTIMATE);
		for (int i = 0; i < right.size(); i++)
		{
			inR[i][0] = right[i];
			inR[i][1] = 0.0;
		}

		fftw_execute(pR);

		double freq_stepR = (double)sampleRate / right.size();
		double cutoff_binR = cuttoff / freq_stepR;
		for (int i = cutoff_binR; i < right.size() - cutoff_binR; i++)
		{
			outR[i][0] = 0.0;
			outR[i][1] = 0.0;
		}

		fftw_plan qR = fftw_plan_dft_1d(right.size(), outR, inR, FFTW_BACKWARD, FFTW_ESTIMATE);
		fftw_execute(qR);

		for (int i = 0; i < right.size(); i++)
		{
			right[i] = inR[i][0] / right.size();
		}

		//Empty Mem
		fftw_destroy_plan(pR);
		fftw_destroy_plan(qR);
		fftw_free(inR);
		fftw_free(outR);
	}

	static void highPassFFTW(std::vector<short>& left, std::vector<short>& right, int sampleRate, int cutoff)//ADD NORMALIZATION FACTOR!!!!!!
	{
		//Left Side
		fftw_complex* in = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * left.size());
		fftw_complex* out = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * left.size());

		fftw_plan p = fftw_plan_dft_1d(left.size(), in, out, FFTW_FORWARD, FFTW_ESTIMATE);

		for (int i = 0; i < left.size(); i++)
		{
			in[i][0] = left[i];
			in[i][1] = 0.0;
		}
		//do fft
		fftw_execute(p);
		//modify spectral data
		double freq_step = (double)sampleRate / left.size();
		double cutoff_bin = cutoff / freq_step;
		for (int i = 0; i < cutoff_bin; i++)
		{
			out[i][0] = 0.0;
			out[i][1] = 0.0;
		}
		for (int i = left.size() - cutoff_bin; i < left.size(); i++)
		{
			out[i][0] = 0.0;
			out[i][1] = 0.0;
		}

		//go back to time domain
		fftw_plan q = fftw_plan_dft_1d(left.size(), out, in, FFTW_BACKWARD, FFTW_ESTIMATE);
		fftw_execute(q);

		//Get max value of Inverted FFT imag and real
		std::vector<double> normsL(left.size());
		for (int i = 0; i < left.size(); i++)
		{
			double real = in[i][0];
			double imag = in[i][1];
			normsL[i] = std::sqrt(real * real + imag * imag);
		}
		double maxValL = *std::max_element(normsL.begin(), normsL.end());



		for (int i = 0; i < left.size(); i++)
		{
			left[i] = in[i][0] / maxValL*32766;
		}

		//Empty Mem
		fftw_destroy_plan(p);
		fftw_destroy_plan(q);
		fftw_free(in);
		fftw_free(out);

		//Right Side
		std::cout << "Did left" << std::endl;
		fftw_complex* inR = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * right.size());
		fftw_complex* outR = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * right.size());

		fftw_plan pR = fftw_plan_dft_1d(right.size(), inR, outR, FFTW_FORWARD, FFTW_ESTIMATE);
		for (int i = 0; i < right.size(); i++)
		{
			inR[i][0] = right[i];
			inR[i][1] = 0.0;
		}

		fftw_execute(pR);

		double freq_stepR = (double)sampleRate / right.size();
		double cutoff_binR = cutoff / freq_stepR;
		for (int i = 0; i < cutoff_bin; i++)
		{
			outR[i][0] = 0.0;
			outR[i][1] = 0.0;
		}
		for (int i = right.size() - cutoff_bin; i < right.size(); i++)
		{
			outR[i][0] = 0.0;
			outR[i][1] = 0.0;
		}

		fftw_plan qR = fftw_plan_dft_1d(right.size(), outR, inR, FFTW_BACKWARD, FFTW_ESTIMATE);
		fftw_execute(qR);
		std::vector<double> norms(right.size());
		for (int i = 0; i < right.size(); i++)
		{
			double real = inR[i][0];
			double imag = inR[i][1];
			norms[i] = std::sqrt(real * real + imag * imag);
		}
		double maxVal = *std::max_element(norms.begin(), norms.end());

		for (int i = 0; i < right.size(); i++)
		{
			right[i] = inR[i][0] / maxVal*32766;
		}

		//Empty Mem
		fftw_destroy_plan(pR);
		fftw_destroy_plan(qR);
		fftw_free(inR);
		fftw_free(outR);
	}

	static void bandPassFFTW(std::vector<short>& left, std::vector<short>& right, int sampleRate, int lowerBound, int higherBound)
	{

	}

	static void lowPassFFTW_HannWindow(std::vector<short>& left, std::vector<short>& right, int sampleRate, int cutoff)
	{
		// Start with Left Side
		fftw_complex* in = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * left.size());
		fftw_complex* out = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * left.size());

		fftw_plan p = fftw_plan_dft_1d(left.size(), in, out, FFTW_FORWARD, FFTW_ESTIMATE);

		for (int i = 0; i < left.size(); i++)
		{
			in[i][0] = left[i];
			in[i][1] = 0.0;
		}

		fftw_execute(p);

		double freq_step = (double)sampleRate / left.size();
		double cutoff_bin = cutoff / freq_step;
		for (int i = cutoff_bin; i < left.size() - cutoff_bin; i++)
		{
			out[i][0] = 0.0;
			out[i][1] = 0.0;
		}

		fftw_plan q = fftw_plan_dft_1d(left.size(), out, in, FFTW_BACKWARD, FFTW_ESTIMATE);
		fftw_execute(q);

		//Get max value of Inverted FFT imag and real
		std::vector<double> normsL(left.size());
		for (int i = 0; i < left.size(); i++)
		{
			double real = in[i][0];
			double imag = in[i][1];
			normsL[i] = std::sqrt(real * real + imag * imag);
		}
		double maxValL = *std::max_element(normsL.begin(), normsL.end());


		for (int i = 0; i < left.size(); i++)
		{
			left[i] = in[i][0] /maxValL *32766;
		}
		//Hann Window
		


		//Empty Mem
		fftw_destroy_plan(p);
		fftw_destroy_plan(q);
		fftw_free(in);
		fftw_free(out);


		//Now Do Right Side
		std::cout << "Did left asdasd" << std::endl;
		fftw_complex* inR = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * right.size());
		fftw_complex* outR = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * right.size());

		fftw_plan pR = fftw_plan_dft_1d(right.size(), inR, outR, FFTW_FORWARD, FFTW_ESTIMATE);
		for (int i = 0; i < right.size(); i++)
		{
			inR[i][0] = right[i];
			inR[i][1] = 0.0;
		}

		fftw_execute(pR);

		double freq_stepR = (double)sampleRate / right.size();
		double cutoff_binR = cutoff / freq_stepR;
		for (int i = cutoff_binR; i < right.size() - cutoff_binR; i++)
		{
			outR[i][0] = 0.0;
			outR[i][1] = 0.0;
		}

		fftw_plan qR = fftw_plan_dft_1d(right.size(), outR, inR, FFTW_BACKWARD, FFTW_ESTIMATE);
		fftw_execute(qR);

		//Get Max value in inverted FFT array imag and real
		std::vector<double> norms(right.size());
		for (int i = 0; i < right.size(); i++) 
		{
			double real = inR[i][0];
			double imag = inR[i][1];
			norms[i] = std::sqrt(real * real + imag * imag);
		}
		double maxVal = *std::max_element(norms.begin(), norms.end());
		


		for (int i = 0; i < right.size(); i++)
		{
			right[i] = inR[i][0] / maxVal *32766;  // float values are still too high, so clipping happens 
		}
		//Hann Window
		

		//Empty Mem
		fftw_destroy_plan(pR);
		fftw_destroy_plan(qR);
		fftw_free(inR);
		fftw_free(outR);
	}

	

};