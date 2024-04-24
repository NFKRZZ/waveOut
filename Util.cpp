#include "Util.h"
#include "GLOBAL.h"
#include <iostream>
#include <cmath>
std::string Util::getEnumString(Keys key) 
{
    for (const auto& keyPair : GLOBAL::keysString) 
    {
        if (static_cast<int>(key) == keyPair.second) 
        {
            return keyPair.first;
        }
    }
    return ""; // Return empty string if the enum value is not found in the vector
}
std::string Util::getEnumString(Key key)
{
    for (const auto& keyPair : GLOBAL::keyString)
    {
        if (static_cast<int>(key) == keyPair.second)
        {
            return keyPair.first;
        }
    }
    return "";
}
std::vector<double> Util::dX(std::vector<double> data)
{
    vector<double> dx;
    double dt = 1 / static_cast<double> (GLOBAL::sampleRate);
    for (int i = 1; i < data.size(); i++)
    {
        dx.push_back((data[i] - data[i - 1]) / dt);
        //cout << "THIS IS RES: "<<data[i] - data[i - 1] <<" /|\\ This is data[i] "<<data[i]<<" This is data[i-1] "<<data[i-1] << endl;
    }
    return dx;
}
std::vector<double> Util::normalizeVector16(std::vector<short>& data, int bitDepth)
{
    int maxPossibleValue = SHRT_MAX;
    int maxAbsValue = std::abs(data[0]);
    for (size_t j = 1; j < data.size(); j++)
    {
        int absVal = std::abs(data[j]);
        if (absVal > maxAbsValue)
        {
            maxAbsValue = absVal;
        }
    }

    double ratio = maxPossibleValue / static_cast<double>(maxAbsValue);
    cout << "THIS IS RATIO " << ratio << endl;
    vector<double> normalizedData(data.size());
    for (size_t i = 0; i < data.size(); i++)
    {
        normalizedData[i] = (data[i] * ratio)/SHRT_MAX;
    }

    return normalizedData;



}
std::vector<short> Util::normalizeVector(std::vector<short> &data)// simply normalizes a vector specifically a 16 bit signed vector
{
    int maxAbsVal = std::abs(data[0]);
    for (size_t i = 1; i < data.size(); i++)
    {
        int absVal = std::abs(data[i]);
        if (absVal > maxAbsVal)
        {
            maxAbsVal = absVal;
        }
    }
    double ratio = SHRT_MAX / static_cast<double>(maxAbsVal);
    vector<short> normalized(data.size());
    for (size_t j = 0; j < data.size(); j++)
    {
        normalized[j] = data[j] * ratio;
    }
    return normalized;
}
std::vector<short> Util::doubleToShortScaled(const std::vector<double>& input) 
{
    std::vector<short> output;

    // Define the scaling factor to map doubles to the range of shorts.
    const double scalingFactor = 32767.0;

    // Find the maximum absolute value in the input vector.
    double maxAbsValue = 0.0;
    for (double value : input) {
        maxAbsValue = std::max(maxAbsValue, std::abs(value));
    }

    // Avoid division by zero if the input vector is empty.
    if (maxAbsValue == 0.0) {
        maxAbsValue = 1.0;
    }

    // Scale and convert the input values to shorts.
    for (double value : input) {
        double scaledValue = (value / maxAbsValue) * scalingFactor;

        // Ensure that the scaled value is within the range of short.
        if (scaledValue > scalingFactor) {
            scaledValue = scalingFactor;
        }
        else if (scaledValue < -scalingFactor) {
            scaledValue = -scalingFactor;
        }

        // Convert the scaled value to short and add it to the output vector.
        output.push_back(static_cast<short>(scaledValue));
    }

    return output;
}
std::vector<double> Util::integrate(vector<double>& data)
{
    double dt = 1 / (static_cast<double>(GLOBAL::sampleRate));
    int n = data.size();
    if (n <= 1) {
        std::cerr << "Error: Data vector must have more than one element." << std::endl;
        return std::vector<double>();
    }

    std::vector<double> integratedData(n);

    double sum = 0.0;
    integratedData[0] = 0.0; // Initial value

    for (int i = 1; i < n; i++) {
        sum += (data[i - 1] + data[i]) / 2.0; // Trapezoidal rule
        integratedData[i] = sum * dt;
    }

    return integratedData;
}
void Util::saveVectorToFile(const std::vector<double>& data, const std::string& filename) 
{
    std::ofstream outfile(filename);
    if (!outfile.is_open()) 
    {
        std::cerr << "Error: Failed to open the file." << std::endl;
        return;
    }
    for (const double& value : data) 
    {
        outfile << value << " ";
    }
    outfile.close();
    std::cout << "Data has been saved to '" << filename << "'" << std::endl;
}
void Util::createRawFile(vector<short>& data,const string& filename)
{
    std::ofstream out(filename, ios::binary);

    if (!out)
    {
        cerr << "error opening file" << endl;
    }
    for (short v : data)
    {
        out.write(reinterpret_cast<const char*>(&v), sizeof(short));
    }
    out.close();
}
void Util::createRawFile(vector<double>& data, const string& filename)
{
    std::ofstream out(filename, ios::binary);

    if (!out)
    {
        cerr << "error opening file" << endl;
    }
    for (double v : data)
    {
        out.write(reinterpret_cast<const char*>(&v), sizeof(double));
    }
    out.close();
}
std::vector<int> Util::noteSegmentation(vector<short> &left, vector<short> &right, vector<short> mono)
{
    //Running average and compare to see if audio sample is n stdev away from avg
    int windowSize = 48000;
    double thresholdMultiplier = 3;
    std::vector<int> lol;
    if (mono.empty() || windowSize <= 0 || mono.size() < static_cast<size_t>(windowSize)) {
        std::cerr << "Invalid input data or window size." << std::endl;
    }

    double sum = 0.0;

    for (int i = 0; i < windowSize; ++i) {
        sum += mono[i];
    }

    for (size_t i = windowSize; i < mono.size(); ++i) {
        double old = mono[i - windowSize];
        sum = sum - old + mono[i];

        double average = sum / windowSize;
        double variance = 0.0;

        for (int j = i - windowSize + 1; j <= static_cast<int>(i); ++j) {
            double diff = mono[j] - average;
            variance += diff * diff;
        }

        variance /= windowSize;

        double stdDev = sqrt(variance);

        if (std::abs(mono[i] - average) > thresholdMultiplier * stdDev) {
            std::cout << "Anomaly detected at index " << i << ": " << mono[i] << std::endl;
        }
    }


    return lol;


}


