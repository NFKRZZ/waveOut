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
	static std::vector<double> calculate_high_pass_filter_coefficients(double sample_rate, double cutoff_frequency, int num_taps) {
		// Calculate the cutoff frequency in radians per sample.
		double wc = 2.0 * 3.141592653589 * cutoff_frequency / sample_rate;

		// Initialize the coefficients vector.
		std::vector<double> coefficients(num_taps, 0.0);

		// Calculate the filter coefficients using the formula for an FIR high pass filter.
		for (int i = 0; i < num_taps; i++) {
			if (i == num_taps / 2) {
				coefficients[i] = wc / 3.141592653589;
			}
			else {
				coefficients[i] = (sin(wc * (i - num_taps / 2))) / (3.141592653589 * (i - num_taps / 2));
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
	void high_pass_filter(std::vector<std::pair<short, short>>& samples,
		const std::vector<double>& coefficients) {
		// We will apply the filter by convolving the coefficients with the samples.
		// Initialize the output vector with zeros.
		std::vector<double> output(samples.size(), 0.0);

		// Convert the input samples to double precision and sum the left and right channels.
		std::vector<double> samples_double(samples.size());
		std::transform(samples.begin(), samples.end(), samples_double.begin(),
			[](const std::pair<short, short>& x) {
			return static_cast<double>(x.first + x.second);
		});

		// Convolve the coefficients with the samples.
		for (size_t i = 0; i < samples_double.size(); i++) {
			for (size_t j = 0; j < coefficients.size(); j++) {
				if (i >= j) {
					output[i] += coefficients[j] * samples_double[i - j];
				}
			}
		}

		// Convert the output back to short precision and copy it back to the input vector.
		std::transform(output.begin(), output.end(), samples.begin(),
			[](double x) {
			short y = static_cast<short>(std::round(x));
			return std::make_pair(y, y);
		});
	}
	static void jhigh_pass_filter(std::vector<short>& left_channel,std::vector<short>& right_channel,const std::vector<double>& coefficients) 
	{
		// We will apply the filter by convolving the coefficients with the samples.
		// Initialize the output vectors with zeros.
		std::vector<double> left_output(left_channel.size(), 0.0);
		std::vector<double> right_output(right_channel.size(), 0.0);

		// Convert the input samples to double precision.
		std::vector<double> left_channel_double(left_channel.size());
		std::vector<double> right_channel_double(right_channel.size());
		std::transform(left_channel.begin(), left_channel.end(), left_channel_double.begin(),
			[](short x) { return static_cast<double>(x); });
		std::transform(right_channel.begin(), right_channel.end(), right_channel_double.begin(),
			[](short x) { return static_cast<double>(x); });

		// Determine the number of samples to process for each channel.
		size_t num_samples = min(left_channel_double.size(), right_channel_double.size());

		// Convolve the coefficients with the samples for each channel.
		for (size_t i = 0; i < num_samples; i++) {
			for (size_t j = 0; j < coefficients.size(); j++) {
				if (i >= j) {
					left_output[i] += coefficients[j] * left_channel_double[i - j];
					right_output[i] += coefficients[j] * right_channel_double[i - j];
				}
			}
		}

		// Convert the output back to short precision and copy it back to the input vectors.
		std::transform(left_output.begin(), left_output.begin() + num_samples, left_channel.begin(),
			[](double x) { return static_cast<short>(std::round(x)); });
		std::transform(right_output.begin(), right_output.begin() + num_samples, right_channel.begin(),
			[](double x) { return static_cast<short>(std::round(x)); });
	}
	static void highPassFilter(pair<vector<double>,vector<double>> LeftRight, int cuttoffFrequency)
	{

	}

};