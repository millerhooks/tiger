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
#include "NeuralLayer.h"
#include "AlloyUnits.h"
#include "AlloyDrawUtil.h"
#include "TigerApp.h"
#include "NeuralFlowPane.h"
#include <cereal/archives/xml.hpp>
#include <cereal/archives/json.hpp>
#include <cereal/archives/portable_binary.hpp>
using namespace aly;
namespace tgr {
std::string MakeID(int len) {
	std::stringstream ss;
	static const char lookUp[33] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ012345";
	for (int i = 0; i < len; i++) {
		ss << lookUp[RandomUniform(0, 31)];
	}
	return ss.str();
}
void WriteNeuralStateToFile(const std::string& file,
		const NeuralState& params) {
	std::string ext = GetFileExtension(file);
	if (ext == "json") {
		std::ofstream os(file);
		cereal::JSONOutputArchive archive(os);
		archive(cereal::make_nvp("neuralstate", params));
	} else if (ext == "xml") {
		std::ofstream os(file);
		cereal::XMLOutputArchive archive(os);
		archive(cereal::make_nvp("neuralstate", params));
	} else {
		std::ofstream os(file, std::ios::binary);
		cereal::PortableBinaryOutputArchive archive(os);
		archive(cereal::make_nvp("neuralstate", params));
	}
}
void ReadNeuralStateFromFile(const std::string& file, NeuralState& params) {
	std::string ext = GetFileExtension(file);
	if (ext == "json") {
		std::ifstream os(file);
		cereal::JSONInputArchive archive(os);
		archive(cereal::make_nvp("neuralstate", params));
	} else if (ext == "xml") {
		std::ifstream os(file);
		cereal::XMLInputArchive archive(os);
		archive(cereal::make_nvp("neuralstate", params));
	} else {
		std::ifstream os(file, std::ios::binary);
		cereal::PortableBinaryInputArchive archive(os);
		archive(cereal::make_nvp("neuralstate", params));
	}
}
NeuralLayer::NeuralLayer(const std::string& name,
		const std::vector<ChannelType>& inTypes,
		const std::vector<ChannelType>& outTypes) :
		id(-1), name(name), inputTypes(inTypes), outputTypes(outTypes) {
	inputChannels = (int) inputTypes.size();
	outputChannels = (int) outputTypes.size();
	inputs.resize(inputChannels);
	outputs.resize(outputChannels);
	initialized = false;
	trainable = true;
	visited = false;
	sys = nullptr;
}
void NeuralLayer::setId(int i) {
	id = i;
}
std::shared_ptr<aly::NeuralFlowPane> NeuralLayer::getFlow() const {
	return sys->getFlow();
}
void NeuralLayer::setRegionDirty(bool b) {
	if (layerRegion.get() != nullptr) {
		layerRegion->setDirty(b);
	}
}
void NeuralLayer::addChild(const std::shared_ptr<NeuralLayer>& layer) {
	children.push_back(layer.get());
	layer->dependencies.push_back(this);
}
bool NeuralLayer::visitedChildren() const {
	for (NeuralLayer* layer : children) {
		if (!layer->isVisited())
			return false;
	}
	return true;
}
bool NeuralLayer::visitedDependencies() const {
	for (NeuralLayer* layer : dependencies) {
		if (!layer->isVisited())
			return false;
	}
	return true;
}
bool NeuralLayer::isVisible() const {
	if (layerRegion.get() != nullptr && layerRegion->parent != nullptr) {
		return layerRegion->isVisible();
	} else {
		return false;
	}
}
aly::NeuralLayerRegionPtr NeuralLayer::getRegion() {
	if (layerRegion.get() == nullptr) {
		float2 dims = float2(240.0f, 240.0f / getAspect())
				+ NeuralLayerRegion::getPadding();
		layerRegion = NeuralLayerRegionPtr(
				new NeuralLayerRegion(name, this,
						CoordPerPX(0.5f, 0.5f, -dims.x * 0.5f, -dims.y * 0.5f),
						CoordPX(dims.x, dims.y)));
		if (hasChildren()) {
			layerRegion->setExpandable(true);
			for (auto child : getChildren()) {
				child->getRegion();
			}
		}
		layerRegion->onHide = [this]() {
			sys->getFlow()->update();
		};
		layerRegion->onExpand = [this]() {
			expand();
		};
	}
	return layerRegion;
}
template<typename Result, typename T, typename Pred>
std::vector<Result> map_(const std::vector<T> &vec, Pred p) {
	std::vector<Result> res(vec.size());
	for (size_t i = 0; i < vec.size(); ++i) {
		res[i] = p(vec[i]);
	}
	return res;
}
void NeuralLayer::setSampleCount(size_t sample_count) {
	// increase the size if necessary - but do not decrease
	auto resize = [sample_count](Tensor*tensor) {
		tensor->resize(sample_count,(*tensor)[0]);
	};
	for (size_t i = 0; i < inputChannels; i++) {
		if (!is_trainable_weight(inputTypes[i])) {
			resize(&getInput(i)->weight);
		}
		resize(&getInput(i)->change);
	}

	for (int i = 0; i < outputChannels; i++) {
		if (!is_trainable_weight(outputTypes[i])) {
			resize(&getOutput(i)->weight);
		}
		resize(&getOutput(i)->change);
	}
}
void NeuralLayer::forward() {
	// the computational graph
	fowardInData.resize(inputChannels);
	fowardInGradient.resize(outputChannels);
	// Organize input/output vectors from storage (computational graph).
	// Internally ith_in_node() will create a connection/edge in the
	// computational graph and will allocate memory in case that it's not
	// done yet.
	for (int i = 0; i < inputChannels; i++) {
		fowardInData[i] = &getInput(i)->weight;
	}
	// resize outs and stuff to have room for every input sample in
	// the batch
	setSampleCount(fowardInData[0]->size());

	// Internally ith_out_node() will create a connection/edge to the
	// computational graph and will allocate memory in case that it's not
	// done yet. In addition, gradient vector are initialized to default
	// values.
	for (int i = 0; i < outputChannels; i++) {
		fowardInGradient[i] = &getOutput(i)->weight;
		getOutput(i)->clearGradients();
	}
	// call the forward computation kernel/routine
	forwardPropagation(fowardInData, fowardInGradient);
}

void NeuralLayer::backward() {
	backwardInData.resize(inputChannels);
	backwardInGradient.resize(inputChannels);
	backwardOutData.resize(outputChannels);
	backwardOutGradient.resize(outputChannels);
	// organize input/output vectors from storage
	for (int i = 0; i < inputChannels; i++) {
		SignalPtr nd = getInput(i);
		backwardInData[i] = &nd->weight;
		backwardInGradient[i] = &nd->change;
	}
	for (int i = 0; i < outputChannels; i++) {
		SignalPtr nd = getOutput(i);
		backwardOutData[i] = &nd->weight;
		backwardOutGradient[i] = &nd->change;
	}
	backwardPropagation(backwardInData, backwardOutData, backwardOutGradient,
			backwardInGradient);
}
std::vector<Tensor> NeuralLayer::backward(
		const std::vector<Tensor>& out_grads) { // for test
	setup(false);
	std::vector<std::vector<const Storage*>> grads2;
	grads2.resize(out_grads.size());
	for (size_t i = 0; i < out_grads.size(); ++i) {
		grads2[i].resize(out_grads[i].size());
		for (size_t j = 0; j < out_grads[i].size(); ++j) {
			grads2[i][j] = &(out_grads[i][j]);
		}
	}
	setOutputGradients(grads2);
	backward();
	return map_<Tensor>(inputs, [](SignalPtr e) {return e->change;});
}
void NeuralLayer::forward(const std::vector<Tensor>& input,
		std::vector<Tensor*>& out) {  // for test
	// allocate data in the computational graph without
	// resetting the weights.
	setup(false);
	std::vector<std::vector<const Storage*>> input2;
	input2.resize(input.size());
	for (size_t i = 0; i < input.size(); ++i) {
		input2[i].resize(input[i].size());
		for (size_t j = 0; j < input[i].size(); ++j) {
			input2[i][j] = &(input[i][j]);
		}
	}
	// the incoming data is forwarded to the computational graph.
	setInputData(input2);
	// pick up the data from the computational graph and perform
	// computation.
	forward();
	// retrieve computed data and return values in form of 4D tensor.
	getOutput(out);
}

void NeuralLayer::getOutput(std::vector<Tensor*>& out) const {
	out.clear();
	for (size_t i = 0; i < outputChannels; i++) {
		if (outputTypes[i] == ChannelType::data) {
			out.push_back(&(getOutput(i)->weight));
		}
	}
}
void NeuralLayer::setOutputGradients(
		const std::vector<std::vector<const Storage*>>& grad) {
	size_t n = 0;
	size_t cnt = grad.size();
	for (size_t i = 0; i < outputChannels; i++) {
		if (outputTypes[i] != ChannelType::data)
			continue;
		Tensor& dst_grad = getOutput(i)->change;
		assert(n < cnt);
		const std::vector<const Storage*>& storage = grad[n++];
		size_t sz = storage.size();
		dst_grad.resize(sz);
		for (size_t j = 0; j < sz; ++j) {
			dst_grad[j] = *storage[j];
		}
	}
}

void NeuralLayer::setInputData(
		const std::vector<std::vector<const Storage*>>& data) {
	size_t n = 0;
	size_t cnt = data.size();
	for (size_t i = 0; i < inputChannels; i++) {
		if (inputTypes[i] != ChannelType::data)
			continue;
		Tensor &dst_data = getInput(i)->weight;
		assert(n < cnt);
		const std::vector<const Storage*>& storage = data[n++];
		size_t sz = storage.size();
		dst_data.resize(sz);
		for (size_t j = 0; j < sz; ++j) {
			dst_data[j] = *storage[j];
		}
	}
}
std::vector<const Storage*> NeuralLayer::getInputWeights() const {
	std::vector<const Storage*> v;
	for (size_t i = 0; i < inputChannels; i++) {
		if (is_trainable_weight(inputTypes[i])) {
			v.push_back(getInput(i)->weight.data());
		}
	}
	return v;
}
std::vector<const Storage*> NeuralLayer::getOutputWeights() const {
	std::vector<const Storage*> v;
	for (size_t i = 0; i < outputChannels; i++) {
		if (is_trainable_weight(outputTypes[i])) {
			v.push_back(getOutput(i)->weight.data());
		}
	}
	return v;
}
std::vector<const Storage*> NeuralLayer::getInputGradient() const {
	std::vector<const Storage*> v;
	for (size_t i = 0; i < inputChannels; i++) {
		if (is_trainable_weight(inputTypes[i])) {
			v.push_back(getInput(i)->change.data());
		}
	}
	return v;
}
std::vector<const Storage*> NeuralLayer::getOutputGradient() const {
	std::vector<const Storage*> v;
	for (size_t i = 0; i < outputChannels; i++) {
		if (is_trainable_weight(outputTypes[i])) {
			v.push_back(getOutput(i)->change.data());
		}
	}
	return v;
}
void NeuralLayer::clearGradients() {
	for (int i = 0; i < inputChannels; i++) {
		getInput(i)->clearGradients();
	}
}
void NeuralLayer::updateWeights(const std::function<void(Storage& dW,Storage& W,bool parallel)>& optimizer, int batch_size) {
	float_t rcp_batch_size = float_t(1) / float_t(batch_size);
	auto &diff = weightDifference;
	for (int i = 0; i < inputChannels;i++) {
		if (trainable && is_trainable_weight(inputTypes[i])) {
			Storage& target = getInputWeights(i);
			getInput(i)->mergeGradients(diff);
			for (size_t j = 0; j < diff.size(); ++j) {
				diff[j] *= rcp_batch_size;
			}
			// parallelize only when target size is big enough to mitigate
			// thread spawning overhead.
			bool parallelize = (target.size() >= 512);
			optimizer(diff, target, parallelize);
		}
	}
	clearGradients();
	post();
}

bool NeuralLayer::hasSameWeights(const NeuralLayer &rhs, float_t eps) const {
	auto w1 = getInputWeights();
	auto w2 = rhs.getInputWeights();
	if (w1.size() != w2.size())
		return false;

	for (size_t i = 0; i < w1.size(); i++) {
		if (w1[i]->size() != w2[i]->size())
			return false;

		for (size_t j = 0; j < w1[i]->size(); j++) {
			if (std::abs(w1[i]->operator [](j) - w2[i]->operator [](j)) > eps)
				return false;
		}
	}
	return true;
}

void NeuralLayer::initializeWeights() {
	// layer/node is not trainable, do nothing and mark the layer/node
	// as initialized.
	if (!trainable) {
		initialized = true;
		return;
	}
	// Fill vector values with data generated by the initialization
	// function. The pointer to the data is obtained from the
	// computational graph and the methods fan_in_size() and fan_out_size()
	// return the number of incoming/outcoming connections for each
	// input/output unit.
	for (size_t i = 0; i < inputChannels; i++) {
		switch (inputTypes[i]) {
		// fill vectors of weight type
		case ChannelType::weight:
			if (weightInitFunc)
				weightInitFunc(getInputWeights(i), getFanInSize(),
						getFanOutSize());
			break;
			// fill vector of bias type
		case ChannelType::bias:
			if (biasInitFunc)
				biasInitFunc(getInputWeights(i), getFanInSize(),
						getFanOutSize());
			break;
		default:
			break;
		}
	}
	// in case we succeed with data initialization, we mark the
	// layer/node as initialized.
	initialized = true;
}
void NeuralLayer::setup(bool reset_weight) {
	// The input shape (width x height x depth) must be equal to the number
	// of input channels a.k.a the number of incoming vectors or 'edges' in
	// the computational nomenclature. Same is applied to output shape and
	// numbers of output edges.
	if (getInputDimensions().size() != inputChannels
			|| getOutputDimensions().size() != outputChannels) {
		throw std::runtime_error("Connection mismatch at setup layer");
	}

	// An 'edge' is created in the computational graph from the current
	// layer/node to each output node and allocates the needed memory.
	// The number of output nodes is determined by the layer interface.
	// In order to handle graph based networks, which a layer/node might
	// have multiple input/output connections, we need to check that the
	// connection edge does not already exists if we don't want duplicated
	// memory allocation.
	for (size_t i = 0; i < outputChannels; i++) {
		if (outputs[i].get() == nullptr) {
			outputs[i] = SignalPtr(
					new Signal(this, getOutputDimensions(i), outputTypes[i]));
		}
	}

	// reset the weights if necessary, or in case that the data is
	// still not initialized.
	if (reset_weight || !initialized) {
		initializeWeights();
	}
}

void NeuralLayer::expand() {
	std::shared_ptr<NeuralFlowPane> flowPane = sys->getFlow();
	box2px bounds = layerRegion->getBounds();
	int N = int(getChildren().size());
	float layoutWidth = 0.0f;
	float width = 120.0f;
	float offset = 0.5f * width;
	layoutWidth = (10.0f + width) * N - 10.0f;
	for (auto child : getChildren()) {
		float height = child->getRegion()->setSize(width);
		float2 pos = pixel2(
				bounds.position.x + bounds.dimensions.x * 0.5f
						- layoutWidth * 0.5f + offset,
				bounds.position.y + bounds.dimensions.y + 0.5f * height
						+ 10.0f);
		flowPane->add(child, pos);
		offset += width + 10.0f;
	}
	flowPane->update();
}
void NeuralLayer::initialize(const aly::ExpandTreePtr& tree,
		const aly::TreeItemPtr& parent) {
	TreeItemPtr item;
	parent->addItem(item = TreeItemPtr(new TreeItem(getName(), 0x0f20e)));
	const float fontSize = 20;
	const int lines = 2;
	item->addItem(
			LeafItemPtr(
					new LeafItem(
							[this,fontSize](AlloyContext* context, const box2px& bounds) {
								NVGcontext* nvg = context->nvgContext;
								float yoff = 2 + bounds.position.y;
								nvgFontSize(nvg, fontSize);
								nvgFontFaceId(nvg, context->getFontHandle(FontType::Normal));
								std::string label;

								label = MakeString() << "In Layers: " << getDependencies().size() <<" Out Layers: "<<getChildren().size();
								drawText(nvg, bounds.position.x, yoff, label.c_str(), FontStyle::Normal, context->theme.LIGHTER);
								yoff += fontSize + 2;

								label = MakeString() << "Size: " << width << " x " << height << " x " << depth;
								drawText(nvg, bounds.position.x, yoff, label.c_str(), FontStyle::Normal, context->theme.LIGHTER);
								yoff += fontSize + 2;

							}, pixel2(180, lines * (fontSize + 2) + 2))));
	item->onSelect = [this](TreeItem* item, const InputEvent& e) {
		sys->getFlow()->setSelected(this,e);

	};
	for (auto child : children) {
		child->initialize(tree, item);
	}
}
}
