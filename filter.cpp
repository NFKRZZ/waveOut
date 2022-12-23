#include <vector>
#include <cmath>
#include <algorithm>
#include <iostream>

using namespace std;
class filter
{
public:
	static std::vector<double> design_high_pass_filter(double cutoff_frequency, int num_taps)
	{
		//Set the sample points for the window method.
		std::vector<double> sample_points(num_taps);
		for (int i = 0; i < num_taps; i++)
		{
			sample_points[i] = -1.0 + 2.0 * i / (num_taps - 1);
		}

		//window function.
		std::vector<double> window(num_taps);
		for (int i = 0; i < num_taps; i++) 
		{
			window[i] = 0.54 - 0.46 * std::cos(2.0 * 3.1415 * i / (num_taps - 1));
		}

		//Calculate the filter coefficients based on the desired frequency response
		std::vector<double> coefficients(num_taps);
		for (int i = 0; i < num_taps; i++) 
		{
			if (i == num_taps / 2) 
			{
				coefficients[i] = 2.0 * 3.1415 * cutoff_frequency;
			}
			else 
			{
				coefficients[i] = std::sin(2.0 * 3.1415 * cutoff_frequency * (i - num_taps / 2)) /(3.1415 * (i - num_taps / 2)) * window[i];
			}
		}
		return coefficients;
	}

	static void one_high_pass_filter(std::vector<short>& samples, const std::vector<double>& coefficients, double cutoff_frequency) 
	{
		// We will apply the filter by convolving the coefficients with the samples.
		// Initialize the output vector with zeros.
		std::vector<short> output(samples.size(), 0.0);
		// Convolve the coefficients with the samples.
		for (size_t i = 0; i < samples.size(); i++)
		{
			for (size_t j = 0; j < coefficients.size(); j++)
			{
				if (i >= j)
				{
					output[i] += coefficients[j] * samples[i - j];
				}
			}
		}

		// Copy the output back to the input vector.
		cout << "Applied high pass filter" << endl;
		samples = output;
	}

	static void high_pass_filter(std::vector<short>& samples, const std::vector<double>& coefficients) 
	{
		// We will apply the filter by convolving the coefficients with the samples.
		// Initialize the output vector with zeros.
		std::vector<double> output(samples.size(), 0.0);

		// Convert the input samples to double precision.
		std::vector<double> samples_double(samples.size());
		std::transform(samples.begin(), samples.end(), samples_double.begin(),[](short x) { return static_cast<double>(x); });

		// Convolve the coefficients with the samples.
		for (size_t i = 0; i < samples_double.size(); i++) 
		{
			for (size_t j = 0; j < coefficients.size(); j++) 
			{
				if (i >= j) 
				{
					output[i] += coefficients[j] * samples_double[i - j];
				}
			}
		}

		// Convert the output back to short precision and copy it back to the input vector.
		std::transform(output.begin(), output.end(), samples.begin(),[](double x) { return static_cast<short>(std::round(x)); });
	}

	static void highPassFilter(pair<vector<double>,vector<double>> LeftRight, int cuttoffFrequency)
	{

	}

};