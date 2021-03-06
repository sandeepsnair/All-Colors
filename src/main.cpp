#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <list>
#include <random>
#include <set>
#include <sstream>
#include <tuple>

#include <opencv2/opencv.hpp>

using namespace cv;
using namespace std;

typedef unsigned char Channel;
typedef int PosComponent;

const int ImageType = CV_8UC3;

typedef Vec<double, 3> ColorDouble;
typedef Vec<Channel, 3> Color;
typedef pair<PosComponent, PosComponent> Pos;

Channel invalidColor = 0;

void SetPixel(Mat& image, PosComponent x, PosComponent y, const Color& color)
{
	image.at<Color>(y, x) = color;
}

Color GetPixel(const Mat& image, PosComponent x, PosComponent y)
{
	return image.at<Color>(y, x);
}

Color GetPixel(const Mat& image, const Pos& pos)
{
	PosComponent x, y;
	tie(x, y) = pos;
	return GetPixel(image, x, y);
}

const PosComponent spread = 1;

set<Pos> GetFreeNeighbours(const Mat& image, Pos pos)
{
	PosComponent x, y;
	tie(x, y) = pos;
	vector<Pos> neighbours;
	neighbours.reserve(8);
	for (PosComponent nx = x-spread; nx <= x+spread; ++nx)
		for (PosComponent ny = y-spread; ny <= y+spread; ++ny)
			neighbours.push_back(Pos(nx, ny));
	set<Pos> result;
	copy_if(neighbours.begin(), neighbours.end(), inserter(result, result.begin()), [&image](Pos pos) -> bool
	{
		PosComponent x, y;
		tie(x, y) = pos;
		if (x < 0 || x >= image.cols || y < 0 || y >= image.rows)
			return false;
		return GetPixel(image, x, y)[0] == invalidColor;
	});
	return result;
}

template<class T>
const T& min3(const T& a, const T& b, const T& c)
{
    return std::min(std::min(a, b), c);
}

template<class T>
const T& max3(const T& a, const T& b, const T& c)
{
    return std::max(std::max(a, b), c);
}

ColorDouble bgr2hsv(Color bgr)
{
	Channel bc = bgr[0];
	Channel gc = bgr[1];
	Channel rc = bgr[2];
	double b = bc / 255.0;
	double g = gc / 255.0;
	double r = rc / 255.0;
	Channel maxc = max3(rc, gc, bc);
	double v = max3(r, g, b);
	double s = v == 0 ? 0 : (v - min3(r, g, b))/v;
	double h = 0;
	double divisor = v - min3(r, g, b);
	if (divisor == 0)
		return ColorDouble(0, 0, 0);
	if (maxc == rc)
		h = 60*(g-b)/divisor;
	else if (maxc == gc)
		h = 120 + 60*(b-r)/divisor;
	else if (maxc == bc)
		h = 240 + 60*(r-g)/divisor;
	else
		assert(false); // If we land here our v is incorrect.
	if (h < 0)
		h += 360;
	return ColorDouble(h, s, v);
}

double ColorDiff(Color bgr1, Color bgr2)
{
	double db = bgr2[0] - bgr1[0];
	double dg = bgr2[1] - bgr1[1];
	double dr = bgr2[2] - bgr1[2];
	return sqrt(db*db + dg*dg + dr*dr);
}

double ColorPosDiff(const Mat& image, Pos pos, Color color)
{
	PosComponent x, y;
	tie(x, y) = pos;

	double diff = 0;
	int colorCount = 0;
	for (PosComponent nx = x-spread; nx <= x+spread; ++nx)
	{
		for (PosComponent ny = y-spread; ny <= y+spread; ++ny)
		{
			if (nx < 0 || nx >= image.cols || ny < 0 || ny >= image.rows)
				continue;
			Color pixelColor = GetPixel(image, nx, ny);
			if (pixelColor[0] == invalidColor)
				continue;
			diff += ColorDiff(color, pixelColor);
			++colorCount;
		}
	}
	// Avoid division by zero.
	double divisor = std::max(colorCount, 1);
	// Square divisor to avoid coral like growing.
	// This also reduces the number of currently open border pixels.
	return diff/(divisor*divisor);
}

Pos FindBestPos(const Mat& image, set<Pos> nextPositions, Color color,
				mt19937& g)
{
	vector<pair<double, Pos>> ratedPositions;
	ratedPositions.reserve(nextPositions.size());
	transform(nextPositions.begin(), nextPositions.end(),
			back_inserter(ratedPositions), [&](Pos pos) -> pair<double, Pos>
	{
		return make_pair(ColorPosDiff(image, pos, color), pos);
	});
	shuffle(ratedPositions.begin(), ratedPositions.end(), g);
	return min_element(ratedPositions.begin(), ratedPositions.end(),
		[](const pair<double, Pos>& rp1, const pair<double, Pos>& rp2)
	{
		return rp1.first < rp2.first;
	})->second;
}

set<Pos> NonBlackPositions(const Mat& img)
{
	set<Pos> result;
	for (int y = 0; y < img.rows; ++y)
		for (int x = 0; x < img.cols; ++x)
			if (img.at<unsigned char>(y, x) > 0)
				result.insert(Pos(x,y));
	return result;
}

pair<Mat, set<Pos>> Init(int argc, char *argv[])
{
	if (argc < 2)
	{
		cout << "Usage: AllColors [2/3/4/imagePath]" << endl;
		return make_pair(Mat(), set<Pos>());
	}

	int num = 0;
	if (argc > 1 && string(argv[1]) == "2") num = 2;
	if (argc > 1 && string(argv[1]) == "3") num = 3;
	if (argc > 1 && string(argv[1]) == "4") num = 4;

	if (!num)
	{
		Mat src = imread(argv[1], CV_LOAD_IMAGE_GRAYSCALE);
		Mat image = Mat(src.size(), ImageType, Scalar_<Channel>(invalidColor));
		return make_pair(image, NonBlackPositions(src));
	}

	Mat image = Mat(1080, 1920, ImageType, Scalar_<Channel>(invalidColor));
	set<Pos> initPositions;
	if (num == 2)
	{
		initPositions.insert(Pos(0.33*image.cols, 0.5*image.rows));
		initPositions.insert(Pos(0.67*image.cols, 0.5*image.rows));
	}
	else if (num == 3)
	{
		initPositions.insert(Pos(0.33*image.cols, 0.4*image.rows));
		initPositions.insert(Pos(0.67*image.cols, 0.4*image.rows));
		initPositions.insert(Pos(0.50*image.cols, 0.69*image.rows));
	}
	else if (num == 4)
	{
		initPositions.insert(Pos(0.33*image.cols, 0.36*image.rows));
		initPositions.insert(Pos(0.67*image.cols, 0.36*image.rows));
		initPositions.insert(Pos(0.36*image.cols, 0.64*image.rows));
		initPositions.insert(Pos(0.64*image.cols, 0.64*image.rows));
	}

	set<Pos> nextPositions;
	for_each(initPositions.begin(), initPositions.end(), [&](const Pos& pos)
	{
		PosComponent plusLength = 5;
		PosComponent x, y;
		tie(x, y) = pos;
		for (PosComponent nx = x-plusLength; nx <= x+plusLength; ++nx)
			nextPositions.insert(Pos(nx, y));
		for (PosComponent ny = y-plusLength; ny <= y+plusLength; ++ny)
			nextPositions.insert(Pos(x, ny));
	});
	return make_pair(image, nextPositions);
}

Mat Embellish(const Mat& image)
{
	Mat ucharImg;
	image.convertTo(ucharImg, CV_8UC3);

	Mat filtered;
	dilate(ucharImg, filtered, Mat(3, 3, CV_8UC1, Scalar(1)));
	medianBlur(filtered, filtered, 3);

	Mat ts;
	vector<Mat> imageChans(3, Mat());
	split(image, imageChans);
	threshold(imageChans[0], ts, invalidColor, 1, THRESH_BINARY_INV);
	Mat tu;
	ts.convertTo(tu, CV_8UC1);
	Mat tuchar;
	cvtColor(tu, tuchar, CV_GRAY2BGR);

	Mat m;
	multiply(filtered, tuchar, m);

	// fill black gaps, but only with half the median color.
	Mat mixed;
	addWeighted(ucharImg, 1.0, m, 0.5, 0.0, mixed);
	return mixed;
}

int main(int argc, char *argv[])
{
	Mat image;
	set<Pos> nextPositions;
	tie(image, nextPositions) = Init(argc, argv);
	if (!image.rows)
		return 1;

	vector<Color> colors;
	int colValues = 64;
	int colMult = 4;
	for(int b = 1; b < colValues; ++b)
		for(int g = 1; g < 2*colValues; ++g)
			for(int r = 1; r < 2*colValues; ++r)
				colors.push_back(Color(colMult*b, colMult*g/2, colMult*r/2));

	random_device rd;
	mt19937 g(1);
	shuffle(colors.begin(), colors.end(), g);

	sort(colors.begin(), colors.end(), [](Color bgr1, Color bgr2) -> bool
	{
		ColorDouble hsv1 = bgr2hsv(bgr1);
		ColorDouble hsv2 = bgr2hsv(bgr2);
		return hsv1[0] < hsv2[0];
	});

	const int saveEveryNFrames = 512;
	const int maxFrames = colors.size();
	const int maxSaves = maxFrames / saveEveryNFrames;
	unsigned long long imgNum = 0;
	while (!colors.empty() && !nextPositions.empty())
	{
		Color color = colors.back();
		colors.pop_back();
		Pos pos = FindBestPos(image, nextPositions, color, g);
		auto nextPositionsIt = nextPositions.find(pos);
		assert(nextPositionsIt != nextPositions.end());
		nextPositions.erase(nextPositionsIt);
		SetPixel(image, pos.first, pos.second, color);
		set<Pos> newFreePos = GetFreeNeighbours(image, pos);
		nextPositions.insert(newFreePos.begin(), newFreePos.end());
		if (colors.size() % saveEveryNFrames == 0)
		{
			stringstream ss;
			ss << setw(4) << setfill('0') << ++imgNum;
			cout << imgNum << "/" << maxSaves << " " << colors.size() << " " << nextPositions.size() << endl;
			Mat outImage = Embellish(image);
			imwrite("./output/image" + ss.str() + ".png", outImage);
		}
	}
}