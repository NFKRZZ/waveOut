#include <vector>
#include <string>

using namespace std;

class FUNCTIONS
{
public:
	static vector<double> short_to_double(vector<short> &data);
	static vector<short> double_to_short(vector<double> &data);
	static pair<vector<short>, vector<short>> split_audio(vector<short> &data);
	static vector<short> consolidate(vector<short> &left,vector<short> &right);
};
