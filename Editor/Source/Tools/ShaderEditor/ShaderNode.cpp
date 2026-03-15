//
//
//

#include "ShaderNode.hpp"
#include "ShaderNodeEditor.hpp"
#include <sstream>
#include <algorithm>
#include <cmath>

namespace Nightbloom
{
	// ============================================================================
	// ShaderNode Base Class
	// ============================================================================

	ShaderNode::ShaderNode(const std::string& name, int id)
		: name(name), id(id), position(100, 100), size(150, 100) {
	}

	std::string ShaderNode::GetOutputVariable(int outputIndex) const {
		return "node" + std::to_string(id) + "_out" + std::to_string(outputIndex);
	}

	void ShaderNode::AddInputPin(const std::string& pinName, PinType type) {
		NodePin pin;
		pin.name = pinName;
		pin.type = type;
		pin.isInput = true;
		pin.nodeId = id;
		pin.pinId = static_cast<int>(inputPins.size());
		inputPins.push_back(pin);
	}

	void ShaderNode::AddOutputPin(const std::string& pinName, PinType type) {
		NodePin pin;
		pin.name = pinName;
		pin.type = type;
		pin.isInput = false;
		pin.nodeId = id;
		pin.pinId = static_cast<int>(outputPins.size());
		outputPins.push_back(pin);
	}

	std::pair<std::string, PinType> ShaderNode::GetInputWithType(const ShaderGraph* graph, int pinIndex, const std::string& defaultValue) const
	{
		if (inputPins[pinIndex].connectedTo >= 0) {
			for (const auto& conn : graph->connections) {
				if (conn.endNode == id && conn.endPin == pinIndex) {
					const ShaderNode* srcNode = graph->GetNode(conn.startNode);
					if (srcNode && conn.startPin < srcNode->outputPins.size()) {
						return {
							srcNode->GetOutputVariable(conn.startPin),
							srcNode->outputPins[conn.startPin].type
						};
					}
					break;
				}
			}
		}
		return { defaultValue, PinType::Float };
	}

	PinType ShaderNode::GetLargerType(PinType a, PinType b) const
	{
		int sizeA = GetTypeSize(a);
		int sizeB = GetTypeSize(b);
		return sizeA >= sizeB ? a : b;
	}

	int ShaderNode::GetTypeSize(PinType type) const
	{
		switch (type) {
		case PinType::Float: return 1;
		case PinType::Vec2: return 2;
		case PinType::Vec3: return 3;
		case PinType::Vec4: return 4;
		default: return 1;
		}
	}

	std::string ShaderNode::GetGLSLType(PinType type) const
	{
		switch (type) {
		case PinType::Float: return "float";
		case PinType::Vec2: return "vec2";
		case PinType::Vec3: return "vec3";
		case PinType::Vec4: return "vec4";
		default: return "float";
		}
	}

	std::string ShaderNode::ConvertToType(const std::string& value, PinType fromType, PinType toType) const
	{
		if (fromType == toType) return value;

		int fromSize = GetTypeSize(fromType);
		int toSize = GetTypeSize(toType);

		if (fromSize < toSize) {
			// Upcast: float to vec2/3/4
			if (fromSize == 1) {
				return GetGLSLType(toType) + "(" + value + ")";
			}
			// vec2 to vec3/4, vec3 to vec4
			std::string zeros = "";
			for (int i = fromSize; i < toSize; i++) {
				zeros += ", 0.0";
			}
			return GetGLSLType(toType) + "(" + value + zeros + ")";
		}
		else {
			// Downcast: take first components
			std::string swizzle = "";
			if (toSize == 1) swizzle = ".x";
			else if (toSize == 2) swizzle = ".xy";
			else if (toSize == 3) swizzle = ".xyz";
			return value + swizzle;
		}
	}

	PinType ShaderNode::GetConnectedType(const ShaderGraph* graph, int pinIndex) const
	{
		for (const auto& conn : graph->connections) {
			if (conn.endNode == id && conn.endPin == pinIndex) {
				const ShaderNode* srcNode = graph->GetNode(conn.startNode);
				if (srcNode) {
					return srcNode->GetOutputType(conn.startPin);
				}
			}
		}
		return PinType::Float; // Default if not connected
	}

	std::string ShaderNode::GetInputValue(const ShaderGraph* graph, int pinIndex) const
	{
		for (const auto& conn : graph->connections) {
			if (conn.endNode == id && conn.endPin == pinIndex) {
				const ShaderNode* srcNode = graph->GetNode(conn.startNode);
				if (srcNode) {
					std::string value = srcNode->GetOutputVariable(conn.startPin);

					// Auto-convert if types don't match
					PinType srcType = srcNode->GetOutputType(conn.startPin);
					PinType dstType = outputPins[0].resolvedType;

					if (srcType != dstType) {
						return ConvertType(value, srcType, dstType);
					}
					return value;
				}
			}
		}

		// Return default based on resolved output type
		if (!outputPins.empty()) {
			return GetDefaultValue(outputPins[0].resolvedType);
		}
		return "1.0";
	}

	std::string ShaderNode::ConvertType(const std::string& value, PinType from, PinType to) const
	{
		if (from == to) return value;

		int fromSize = GetTypeSize(from);
		int toSize = GetTypeSize(to);

		if (fromSize < toSize) {
			// Broadcast scalar to vector
			if (fromSize == 1) {
				switch (toSize) {
				case 2: return "vec2(" + value + ", " + value + ")";
				case 3: return "vec3(" + value + ", " + value + ", " + value + ")";
				case 4: return "vec4(" + value + ", " + value + ", " + value + ", " + value + ")";
				}
			}
			// Pad with zeros
			std::string zeros = "";
			for (int i = fromSize; i < toSize; i++) {
				zeros += ", 0.0";
			}
			return GetGLSLType(to) + "(" + value + zeros + ")";
		}
		else {
			// Extract components
			std::string swizzle = ".x";
			if (toSize == 2) swizzle = ".xy";
			else if (toSize == 3) swizzle = ".xyz";
			return value + swizzle;
		}
	}

	std::string ShaderNode::GetDefaultValue(PinType type) const
	{
		switch (type) {
		case PinType::Float: return "1.0";
		case PinType::Vec2: return "vec2(1.0)";
		case PinType::Vec3: return "vec3(1.0)";
		case PinType::Vec4: return "vec4(1.0)";
		default: return "1.0";
		}
	}

	// ============================================================================
	// Concrete Node Implementations
	// ============================================================================

	TextureNode::TextureNode(int id) : ShaderNode("Texture", id) {
		AddOutputPin("Color", PinType::Vec4);
		AddOutputPin("R", PinType::Float);
		AddOutputPin("G", PinType::Float);
		AddOutputPin("B", PinType::Float);
		AddOutputPin("A", PinType::Float);
		size = ImVec2(140, 120);
	}

	std::string TextureNode::GenerateGLSL(const ShaderGraph* graph) const {
		std::stringstream ss;
		ss << "vec4 " << GetOutputVariable(0) << " = texture(diffuseTexture, fragTexCoord);\n";
		ss << "float " << GetOutputVariable(1) << " = " << GetOutputVariable(0) << ".r;\n";
		ss << "float " << GetOutputVariable(2) << " = " << GetOutputVariable(0) << ".g;\n";
		ss << "float " << GetOutputVariable(3) << " = " << GetOutputVariable(0) << ".b;\n";
		ss << "float " << GetOutputVariable(4) << " = " << GetOutputVariable(0) << ".a;\n";
		return ss.str();
	}

	void TextureNode::DrawProperties() {
		ImGui::DragInt("Slot", &textureSlot, 0.1f, 0, 7);
	}

	ColorNode::ColorNode(int id) : ShaderNode("Color", id) {
		AddOutputPin("RGBA", PinType::Vec4);
		AddOutputPin("RGB", PinType::Vec3);
		size = ImVec2(140, 100);
	}

	std::string ColorNode::GenerateGLSL(const ShaderGraph* graph) const {
		std::stringstream ss;
		ss << "vec4 " << GetOutputVariable(0) << " = vec4("
			<< color[0] << ", " << color[1] << ", " << color[2] << ", " << color[3] << ");\n";
		ss << "vec3 " << GetOutputVariable(1) << " = " << GetOutputVariable(0) << ".rgb;\n";
		return ss.str();
	}

	void ColorNode::DrawProperties() {
		ImGui::ColorEdit4("Color", color);
	}

	TimeNode::TimeNode(int id) : ShaderNode("Time", id) {
		AddOutputPin("Time", PinType::Float);
		AddOutputPin("Sin", PinType::Float);
		AddOutputPin("Cos", PinType::Float);
		size = ImVec2(120, 90);
	}

	std::string TimeNode::GenerateGLSL(const ShaderGraph* graph) const {
		std::stringstream ss;
		ss << "float " << GetOutputVariable(0) << " = time;\n";
		ss << "float " << GetOutputVariable(1) << " = sin(time);\n";
		ss << "float " << GetOutputVariable(2) << " = cos(time);\n";
		return ss.str();
	}

	MultiplyNode::MultiplyNode(int id) : ShaderNode("Multiply", id) {
		AddInputPin("A", PinType::Any);
		AddInputPin("B", PinType::Any);
		AddOutputPin("Result", PinType::Any);
		size = ImVec2(120, 80);
	}

	void MultiplyNode::ResolveTypes(const ShaderGraph* graph)
	{
		// Find the types of connected inputs
		PinType typeA = GetConnectedType(graph, 0);
		PinType typeB = GetConnectedType(graph, 1);

		// Output type is the larger of the two inputs
		outputPins[0].resolvedType = GetLargerType(typeA, typeB);
	}

	std::string MultiplyNode::GenerateGLSL(const ShaderGraph* graph) const {
		std::stringstream ss;

		// Get the resolved output type
		PinType outputType = outputPins[0].resolvedType;
		if (outputType == PinType::Any) {
			outputType = PinType::Float; // Fallback if not resolved
		}

		std::string outputTypeStr = GetGLSLType(outputType);

		// Get inputs and convert them to the output type
		std::string inputA = GetDefaultValue(outputType);
		std::string inputB = GetDefaultValue(outputType);

		// Check input A
		for (const auto& conn : graph->connections) {
			if (conn.endNode == id && conn.endPin == 0) {
				const ShaderNode* srcNode = graph->GetNode(conn.startNode);
				if (srcNode) {
					std::string value = srcNode->GetOutputVariable(conn.startPin);
					PinType srcType = srcNode->GetOutputType(conn.startPin);
					inputA = ConvertType(value, srcType, outputType);
				}
				break;
			}
		}

		// Check input B
		for (const auto& conn : graph->connections) {
			if (conn.endNode == id && conn.endPin == 1) {
				const ShaderNode* srcNode = graph->GetNode(conn.startNode);
				if (srcNode) {
					std::string value = srcNode->GetOutputVariable(conn.startPin);
					PinType srcType = srcNode->GetOutputType(conn.startPin);
					inputB = ConvertType(value, srcType, outputType);
				}
				break;
			}
		}

		ss << outputTypeStr << " " << GetOutputVariable(0)
			<< " = " << inputA << " * " << inputB << ";\n";

		return ss.str();
	}

	AddNode::AddNode(int id) : ShaderNode("Add", id) {
		AddInputPin("A", PinType::Vec4);
		AddInputPin("B", PinType::Vec4);
		AddOutputPin("Result", PinType::Vec4);
		size = ImVec2(120, 80);
	}

	std::string AddNode::GenerateGLSL(const ShaderGraph* graph) const {
		std::stringstream ss;

		std::string inputA = "vec4(0.0)";
		std::string inputB = "vec4(0.0)";

		if (inputPins[0].connectedTo >= 0) {
			for (const auto& conn : graph->connections) {
				if (conn.endNode == id && conn.endPin == 0) {
					const ShaderNode* srcNode = graph->GetNode(conn.startNode);
					if (srcNode) {
						inputA = srcNode->GetOutputVariable(conn.startPin);
					}
					break;
				}
			}
		}

		if (inputPins[1].connectedTo >= 0) {
			for (const auto& conn : graph->connections) {
				if (conn.endNode == id && conn.endPin == 1) {
					const ShaderNode* srcNode = graph->GetNode(conn.startNode);
					if (srcNode) {
						inputB = srcNode->GetOutputVariable(conn.startPin);
					}
					break;
				}
			}
		}

		ss << "vec4 " << GetOutputVariable(0) << " = " << inputA << " + " << inputB << ";\n";
		return ss.str();
	}

	FragmentOutputNode::FragmentOutputNode(int id) : ShaderNode("Output", id) {
		AddInputPin("Color", PinType::Vec4);
		size = ImVec2(120, 60);
	}

	std::string FragmentOutputNode::GenerateGLSL(const ShaderGraph* graph) const {
		std::string inputColor = "vec4(1.0, 0.0, 1.0, 1.0)"; // Default purple

		if (inputPins[0].connectedTo >= 0) {
			for (const auto& conn : graph->connections) {
				if (conn.endNode == id && conn.endPin == 0) {
					const ShaderNode* srcNode = graph->GetNode(conn.startNode);
					if (srcNode) {
						inputColor = srcNode->GetOutputVariable(conn.startPin);
					}
					break;
				}
			}
		}

		return "outColor = " + inputColor + ";\n";
	}

	MixNode::MixNode(int id) : ShaderNode("Mix", id)
	{
		AddInputPin("A", PinType::Float);
		AddInputPin("B", PinType::Float);
		AddInputPin("Factor", PinType::Float);
		AddOutputPin("Result", PinType::Float);
		size = ImVec2(120, 100);
	}

	std::string MixNode::GenerateGLSL(const ShaderGraph* graph) const
	{
		std::stringstream ss;

		auto [inputA, typeA] = GetInputWithType(graph, 0, "0.0");
		auto [inputB, typeB] = GetInputWithType(graph, 1, "1.0");
		auto [factor, typeFactor] = GetInputWithType(graph, 2, "0.5");

		// Determine output type
		PinType outputType = GetLargerType(typeA, typeB);

		// Convert all to match
		std::string convertedA = ConvertToType(inputA, typeA, outputType);
		std::string convertedB = ConvertToType(inputB, typeB, outputType);
		std::string convertedFactor = ConvertToType(factor, typeFactor, outputType);

		std::string outputTypeStr = GetGLSLType(outputType);
		ss << outputTypeStr << " " << GetOutputVariable(0)
			<< " = mix(" << convertedA << ", " << convertedB << ", " << convertedFactor << ");\n";

		return ss.str();
	}
}