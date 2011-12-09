// kate: replace-tabs off; indent-width 4; indent-mode normal
// vim: ts=4:sw=4:noexpandtab
/*

Copyright (c) 2010--2011,
François Pomerleau and Stephane Magnenat, ASL, ETHZ, Switzerland
You can contact the authors at <f dot pomerleau at gmail dot com> and
<stephane at magnenat dot net>

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the <organization> nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL ETH-ASL BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#include "PointMatcher.h"

//! Construct without parameter
template<typename T>
PointMatcher<T>::ErrorMinimizer::ErrorMinimizer():
	pointUsedRatio(-1.),
	weightedPointUsedRatio(-1.)
{}

//! virtual destructor
template<typename T>
PointMatcher<T>::ErrorMinimizer::~ErrorMinimizer()
{}

//! Constructor from existing data
template<typename T>
PointMatcher<T>::ErrorMinimizer::ErrorElements::ErrorElements(const DataPoints& reading, const DataPoints reference, const OutlierWeights weights, const Matches matches):
	reading(reading),
	reference(reference),
	weights(weights),
	matches(matches)
{
	assert(reading.features.cols() == reference.features.cols());
	assert(reading.features.cols() == weights.cols());
	assert(reading.features.cols() == matches.dists.cols());
	// May have no descriptors... size 0
}

//! Return the ratio of how many points where used for error minimization
template<typename T>
T PointMatcher<T>::ErrorMinimizer::getPointUsedRatio() const
{
	return pointUsedRatio;
}

//! Return the ratio of how many points where used (with weight) for error minimization
template<typename T>
T PointMatcher<T>::ErrorMinimizer::getWeightedPointUsedRatio() const
{
	return weightedPointUsedRatio;
}

//! Helper funtion doing the cross product in 3D and a pseudo cross product in 2D
template<typename T>
typename PointMatcher<T>::Matrix PointMatcher<T>::ErrorMinimizer::crossProduct(const Matrix& A, const Matrix& B)
{
	// Expecting matched points
	assert(A.cols() == B.cols());

	// Expecting homogenous coord X eucl. coord
	assert(A.rows() -1 == B.rows());

	// Expecting homogenous coordinates
	assert(A.rows() == 4 || A.rows() == 3);
	
	const unsigned int x = 0;
	const unsigned int y = 1;
	const unsigned int z = 2;

	Matrix cross;
	if(A.rows() == 4)
	{
		cross = Matrix(B.rows(), B.cols());
				
		cross.row(x) = A.row(y).cwise() * B.row(z) - A.row(z).cwise() * B.row(y);
		cross.row(y) = A.row(z).cwise() * B.row(x) - A.row(x).cwise() * B.row(z);
		cross.row(z) = A.row(x).cwise() * B.row(y) - A.row(y).cwise() * B.row(x);
	}
	else
	{
		cross = Vector(B.cols());
		cross = A.row(x).cwise() * B.row(y) - A.row(y).cwise() * B.row(x);
	}

	return cross;
}


//! Helper function outputting pair of points from the reference and 
//! the reading based on the matching matrix
template<typename T>
typename PointMatcher<T>::ErrorMinimizer::ErrorElements PointMatcher<T>::ErrorMinimizer::getMatchedPoints(
		const DataPoints& requestedPts,
		const DataPoints& sourcePts,
		const Matches& matches, 
		const OutlierWeights& outlierWeights)
{
	typedef typename DataPoints::Features Features;
	typedef typename DataPoints::Descriptors Descriptors;
	typedef typename Matches::Ids Ids;
	typedef typename Matches::Dists Dists;
	
	assert(matches.ids.rows() > 0);
	assert(matches.ids.cols() > 0);
	assert(matches.ids.cols() == requestedPts.features.cols()); //nbpts
	assert(outlierWeights.rows() == matches.ids.rows());  // knn
	
	const int knn = outlierWeights.rows();
	const int dimFeat = requestedPts.features.rows();
	const int dimReqDesc = requestedPts.descriptors.rows();

	// Count points with no weights
	const int pointsCount = (outlierWeights.cwise() != 0.0).count();
	if (pointsCount == 0)
		throw ConvergenceError("no point to minimize");

	Features keptFeat(dimFeat, pointsCount);
	
	Descriptors keptDesc;
	if(dimReqDesc > 0)
		keptDesc = Descriptors(dimReqDesc, pointsCount);

	Matches keptMatches (Dists(1,pointsCount), Ids(1, pointsCount));
	OutlierWeights keptWeights(1, pointsCount);

	int j = 0;
	weightedPointUsedRatio = 0;
	for(int k = 0; k < knn; k++) // knn
	{
		for (int i = 0; i < requestedPts.features.cols(); ++i) //nb pts
		{
			if (outlierWeights(k,i) != 0.0)
			{
				if(dimReqDesc > 0)
					keptDesc.col(j) = requestedPts.descriptors.col(i);
				
				keptFeat.col(j) = requestedPts.features.col(i);
				keptMatches.ids(0, j) = matches.ids(k, i);
				keptMatches.dists(0, j) = matches.dists(k, i);
				keptWeights(0,j) = outlierWeights(k,i);
				++j;
				weightedPointUsedRatio += outlierWeights(k,i);
			}
		}
	}

	//FIXME: This is not true with multiple knn
	this->pointUsedRatio = double(j)/double(requestedPts.features.cols());
	this->weightedPointUsedRatio /= double(requestedPts.features.cols());
	
	assert(dimFeat == sourcePts.features.rows());
	const int dimSourDesc = sourcePts.descriptors.rows();
	
	Features associatedFeat(dimFeat, pointsCount);
	Descriptors associatedDesc;
	if(dimSourDesc > 0)
		associatedDesc = Descriptors(dimSourDesc, pointsCount);

	// Fetch matched points
	for (int i = 0; i < pointsCount; ++i)
	{
		const int refIndex(keptMatches.ids(i));
		associatedFeat.col(i) = sourcePts.features.block(0, refIndex, dimFeat, 1);
		
		if(dimSourDesc > 0)
			associatedDesc.col(i) = sourcePts.descriptors.block(0, refIndex, dimSourDesc, 1);
	}

	DataPoints keptPts;
	keptPts.features = keptFeat;
	keptPts.descriptors = keptDesc;
	keptPts.descriptorLabels = requestedPts.descriptorLabels;

	DataPoints associatedPts;
	associatedPts.features = associatedFeat;
	associatedPts.descriptors = associatedDesc;
	associatedPts.descriptorLabels = sourcePts.descriptorLabels;

	return ErrorElements(keptPts, associatedPts, keptWeights, keptMatches);
}

template struct PointMatcher<float>;
template struct PointMatcher<double>;
