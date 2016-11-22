#include "AveragePoolFilter.h"
#include "AlloyMath.h"
using namespace aly;
namespace tgr {
	AveragePoolFilter::AveragePoolFilter(const std::vector<NeuralLayerPtr>& inputLayers, int kernelSize):kernelSize(kernelSize) {
		NeuralFilter::inputLayers = inputLayers;
		for (NeuralLayerPtr layer : inputLayers) {
			if (layer->width%kernelSize != 0 || layer->height%kernelSize != 0) {
				throw std::runtime_error("Map size must be divisible by kernel size.");
			}
		}
	}
	void AveragePoolFilter::initialize(NeuralSystem& sys) {
		std::vector<SignalPtr> signals;
		outputLayers.resize(inputLayers.size());
		for (int k = 0; k < inputLayers.size(); k++) {
			NeuralLayerPtr inputLayer = inputLayers[k];
			outputLayers[k] = NeuralLayerPtr(new NeuralLayer(MakeString()<<"subsample",inputLayer->width/kernelSize,inputLayer->height/kernelSize,1,true));
			NeuralLayerPtr outputLayer = outputLayers[k];
			sys.add(outputLayer->getBiasSignals());
			for (int j = 0; j < outputLayer->height; j++) {
				for (int i = 0; i < outputLayer->width; i++) {
					SignalPtr sig = SignalPtr(new Signal(RandomUniform(0.0f, 1.0f)));
					Neuron& dest = outputLayer->get(i, j);
					dest.addInput(sig);
					sys.add(sig);
					for (int jj = 0; jj < kernelSize; jj++) {
						for (int ii = 0; ii < kernelSize; ii++) {
							Neuron& src=inputLayer->get(i*kernelSize + ii, j*kernelSize + jj);
							src.addOutput(sig);
						}
					}
				}
			}
		}

	}
}