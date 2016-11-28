/*
* Copyright(C) 2016, Blake C. Lucas, Ph.D. (img.science@gmail.com)
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
* THE SOFTWARE.
*/
#ifndef _TIGERAPP_H_
#define _TIGERAPP_H_
#include "AlloyApplication.h"
#include "AlloyWidget.h"
#include "AlloyVector.h"
#include "AlloyWorker.h"
#include "AlloyImage.h"
#include "NeuralSystem.h"
#include "AlloyExpandTree.h"
#include "NeuralFlowPane.h"
#include "AlloyTimeline.h"
class TigerApp : public aly::Application {
protected:
	tgr::NeuralLayer* selectedLayer;
	bool parametersDirty;
	bool frameBuffersDirty;
	bool running = false;
	tgr::NeuralSystem sys;
	aly::IconButtonPtr playButton, stopButton;
	aly::ExpandTreePtr expandTree;
	aly::DrawPtr dragIconPane;
	aly::NeuralFlowPanePtr flowRegion;
	aly::TimelineSliderPtr timelineSlider;
	aly::Number epochs;
	aly::Number sampleIndex;
	int minIndex,maxIndex;
	aly::Number iterationsPerEpoch;
	aly::Number learningRateInitial;
	aly::Number learningRateDelta;
	std::string trainFile;
	std::string evalFile;
	std::string trainLabelFile;
	std::string evalLabelFile;
	aly::HorizontalSliderPtr tweenRegion;
	std::vector<aly::Image1f> trainInputData;
	std::vector<uint8_t> trainOutputData;
	tgr::NeuralLayerPtr inputLayer;
	tgr::NeuralLayerPtr outputLayer;
	void initialize();
public:
	void setSampleIndex(int idx);
	void setSampleRange(int mn, int mx);
	bool overTarget = false;
	TigerApp();
	virtual void draw(aly::AlloyContext* context) override;
	bool init(aly::Composite& rootNode);
	void evaluate();
	void train(const std::vector<int>& sampleIndexes);
	bool onEventHandler(aly::AlloyContext* context, const aly::InputEvent& e);
	void setSelectedLayer(tgr::NeuralLayer* layer);
};

#endif
