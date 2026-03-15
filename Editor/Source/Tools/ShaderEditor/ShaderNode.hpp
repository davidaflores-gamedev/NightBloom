#pragma once

#include <imgui.h>
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>

namespace Nightbloom
{
	class ShaderGraph;

	// Pin types for node connections
	enum class PinType
	{
		Any,
		Float,
		Vec2,
		Vec3,
		Vec4,
		Texture2D,
		Matrix4
	};

	// Visual style for pins
	struct PinStyle {
		ImU32 color;
		float radius = 5.0f;
	};

	// Node pin (input/outpu)
	struct NodePin
	{
		std::string name;
		PinType type;
		PinType resolvedType = PinType::Any; // For type resolution
		bool isInput;
		int nodeId;
		int pinId;

		// For output pins - can connect to multiple inputs
		std::vector<int> connections;

		// For input pins - single connection
		int connectedTo = -1;

		ImVec2 pos; // Screen position for rendering
	};

	// Connection between two pins
	struct NodeConnection {
		int id;
		int startNode;
		int startPin;
		int endNode;
		int endPin;
	};

	// Base class for shader nodes
	class ShaderNode {
	public:
		ShaderNode(const std::string& name, int id);
		virtual ~ShaderNode() = default;

		// Generate GLSL code for this node
		virtual std::string GenerateGLSL(const ShaderGraph* graph) const = 0;

		virtual void ResolveTypes(const ShaderGraph* graph) {
			// Default implementation for nodes with fixed types
			for (auto& pin : outputPins) {
				if (pin.type != PinType::Any) {
					pin.resolvedType = pin.type;
				}
			}
		}

		// Get the resolved type of an output pin
		PinType GetOutputType(int pinIndex) const {
			if (pinIndex < outputPins.size()) {
				return outputPins[pinIndex].resolvedType;
			}
			return PinType::Float;
		}

		// Get variable name for this node's output
		virtual std::string GetOutputVariable(int outputIndex = 0) const;

		// UI
		virtual void DrawProperties() {} // Override for custom properties

		// Node data
		std::string name;
		int id;
		ImVec2 position;
		ImVec2 size;

		std::vector<NodePin> inputPins;
		std::vector<NodePin> outputPins;

	protected:
		void AddInputPin(const std::string& name, PinType type);
		void AddOutputPin(const std::string& name, PinType type);


		std::pair<std::string, PinType> GetInputWithType(const ShaderGraph* graph, int pinIndex, const std::string& defaultValue) const;

		PinType GetLargerType(PinType a, PinType b) const;

		int GetTypeSize(PinType type) const;

		std::string GetGLSLType(PinType type) const;

		std::string ConvertToType(const std::string& value, PinType fromType, PinType toType) const;

		PinType GetConnectedType(const ShaderGraph* graph, int pinIndex) const;

		std::string GetInputValue(const ShaderGraph* graph, int pinIndex) const;

		std::string ConvertType(const std::string& value, PinType from, PinType to) const;

		std::string GetDefaultValue(PinType type) const;
	};

	// Concrete node types
	class TextureNode : public ShaderNode {
	public:
		TextureNode(int id);
		std::string GenerateGLSL(const ShaderGraph* graph) const override;
		void DrawProperties() override;

		int textureSlot = 0;
	};

	class ColorNode : public ShaderNode {
	public:
		ColorNode(int id);
		std::string GenerateGLSL(const ShaderGraph* graph) const override;
		void DrawProperties() override;

		float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	};

	class TimeNode : public ShaderNode {
	public:
		TimeNode(int id);
		std::string GenerateGLSL(const ShaderGraph* graph) const override;
	};

	class MultiplyNode : public ShaderNode {
	public:
		MultiplyNode(int id);
		void ResolveTypes(const ShaderGraph* graph) override;
		std::string GenerateGLSL(const ShaderGraph* graph) const override;

	private:

	};

	class AddNode : public ShaderNode {
	public:
		AddNode(int id);
		std::string GenerateGLSL(const ShaderGraph* graph) const override;
	};

	class MixNode : public ShaderNode {
	public:
		MixNode(int id);
		std::string GenerateGLSL(const ShaderGraph* graph) const override;
	};

	class FragmentOutputNode : public ShaderNode {
	public:
		FragmentOutputNode(int id);
		std::string GenerateGLSL(const ShaderGraph* graph) const override;
	};
}