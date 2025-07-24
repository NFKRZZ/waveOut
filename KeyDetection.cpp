#include "KeyDetection.h"
#include <utility>
#include <cmath>
#include <math.h>
#include <fftw3.h>
# define M_PI           3.14159265358979323846
#define FFTW_ESTIMATE 64
#define FFTW_MEASURE 0 
namespace KeyDetection
{
	const std::array<double, 12> C_MAJOR_RAW = { {
	  6.35, 2.23, 3.48, 2.33,
	  4.38, 4.09, 2.52, 5.19,
	  2.39, 3.66, 2.29, 2.88
	} };

	const std::array<double, 12> C_MINOR_RAW = { {
	  6.33, 2.68, 3.52, 5.38,
	  2.60, 3.53, 2.54, 4.75,
	  3.98, 2.69, 3.34, 5.29
	} };
	std::vector<double> makeHann(int N)
	{
		std::vector<double> w(N);

		for (int n = 0; n < N; n++)
		{
			w[n] = 0.5 * (1.0 - std::cos(2.0 * M_PI * n / (N - 1)));
		}

		return w;
	}



	std::array<double, 12> getMeanChroma(const std::vector<double>& audio, int fs)
	{
		int Nf = static_cast<int>(audio.size());

		const int N = 4096;
		const int H = 1024;

		std::vector<double> hann(N);
		hann = makeHann(N);


		std::vector<double> in(N);
		fftw_complex* out = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * (N / 2 + 1));


		fftw_plan plan = fftw_plan_dft_r2c_1d(N, in.data(), out, FFTW_ESTIMATE);

		std::array<double, 12> C = { {0} };
		int frames = 0;

		for (int start = 0; start + N <= Nf; start += H)
		{
			for (int n = 0; n < N; ++n)
			{
				in[n] = audio[start + n] * hann[n];
			}
			// FFT
			fftw_execute(plan);

			//per frame chroma
			std::array<double, 12> frameChroma = { {0} };

			for (int k = 0; k <= N / 2; k++)
			{
				double mag = std::hypot(out[k][0], out[k][1]);
				double freq = (double)k / N * fs;
				if (freq < 20.0) continue;
				int midi = int(std::round(69 + 12 * std::log2(freq / 440.0)));
				int pc = (midi % 12 + 12) % 12;
				frameChroma[pc] += mag;
			}
			//accumulate the chromas
			for (int i = 0; i < 12; ++i)
			{
				C[i] += frameChroma[i];
			}
			frames++;
		}

		fftw_destroy_plan(plan);

		//norm
		double sum = 0;
		for (double v : C)
		{
			sum += v * v;
		}
		double norm = std::sqrt(sum);
		if (norm > 0)
		{
			for (double& v : C)
			{
				v /= norm;
			}
		}

		return C;

	}
















	//rotate the weights by k semitones
	std::array<double, 12> rotateBy(const std::array<double, 12>& base, int k)
	{
		std::array<double, 12> out;
		for (int i = 0; i < 12; i++)
		{
			int index = (i - k + 12) % 12;
			out[i] = base[index];
		}

		return out;
	}


	void buildKeyProfiles(std::array<std::array<double, 12>, 24>& outProfiles, std::array<std::string, 24>& outNames)
	{
		auto maj0 = C_MAJOR_RAW;
		auto min0 = C_MINOR_RAW;

		normalize(maj0);
		normalize(min0);

		static constexpr const char* MAJ_NAMES[12] =
		{
		   "C Major","C# Major","D Major","D# Major","E Major","F Major",
		   "F# Major","G Major","G# Major","A Major","A# Major","B Major"
		};
		static constexpr const char* MIN_NAMES[12] =
		{
			"C Minor","C# Minor","D Minor","D# Minor","E Minor","F Minor",
			"F# Minor","G Minor","G# Minor","A Minor","A# Minor","B Minor"
		};

		for (int k = 0; k < 12; k++)
		{
			outProfiles[k] = rotateBy(maj0, k);
			outProfiles[12 + k] = rotateBy(min0, k);
			outNames[k] = MAJ_NAMES[k];
			outNames[12 + k] = MIN_NAMES[k];
		}


	}


	void applyHannWindow(std::vector<double>& frame, const std::vector<double>& hann)
	{
		int N = frame.size();
		for (int n = 0; n < N; n++)
		{
			frame[n] *= hann[n];
		}
	}

	void normalize(std::array<double, 12>& v)
	{
		//get norm
		double norm = 0.0;
		for (int i = 0; i < 12; i++)
		{
			norm += v[i] * v[i];
		}

		norm = std::sqrt(norm);

		//divide by norm

		for (int i = 0; i < 12; i++)
		{
			v[i] /= norm;
		}


	}

	double cosineSimilarity(const std::array<double, 12>& a, const std::array<double, 12>& b)
	{
		double dot = 0;
		for (int i = 0; i < 12; i++)
		{
			dot += a[i] * b[i];
		}
		return dot;
	}

	int detectKey(const std::array<double, 12>& normalizedChroma, const std::array<std::array<double, 12>, 24>& profiles)
	{
		int bestKey = 0;
		double bestScore = -1.0;

		for (int k = 0; k < 24; k++)
		{
			double score = cosineSimilarity(normalizedChroma, profiles[k]);
			if (score > bestScore)
			{
				bestScore = score;
				bestKey = k;
			}
		}
		return bestKey;

	}
}