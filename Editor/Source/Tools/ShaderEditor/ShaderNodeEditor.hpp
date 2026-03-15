#pragma once

#include "ShaderNode.hpp"

namespace Nightbloom
{

	// Shader graph manager
	class ShaderGraph {
	public:
		ShaderGraph();

		// Node management
		void AddNode(std::unique_ptr<ShaderNode> node);
		void RemoveNode(int nodeId);
		ShaderNode* GetNode(int nodeId);
		const ShaderNode* GetNode(int nodeId) const;

		// Connection management
		bool CanConnect(int startNode, int startPin, int endNode, int endPin) const;
		void Connect(int startNode, int startPin, int endNode, int endPin);
		void Disconnect(int connectionId);

		// Code generation
		std::string GenerateFragmentShader() const;
		std::string GenerateVertexShader() const;

		// Utility
		std::vector<int> GetTopologicalSort() const;
		void RefreshShaderInfo();
		void ResolveAllTypes();
		bool UsesTextures() const { return usesTextures; }

		// Data
		std::vector<std::unique_ptr<ShaderNode>> nodes;
		std::vector<NodeConnection> connections;
		int nextNodeId = 1;
		int nextConnectionId = 1;

		bool usesTextures = false;
	};

	// Main editor class
	class ShaderNodeEditor {
	public:
		ShaderNodeEditor();
		~ShaderNodeEditor();

		// Main UI function - call this in your ImGui loop
		void Draw(const char* title, bool* p_open = nullptr);

		// Shader compilation
		bool CompileShaders();
		std::string GetLastError() const { return lastError; }

		// Get generated shaders
		std::string GetVertexShader() const { return vertexShaderCode; }
		std::string GetFragmentShader() const { return fragmentShaderCode; }

				// Utility
		bool UsesTextures() const { return graph ? graph->UsesTextures() : false; }

	private:
		// UI State
		ImVec2 scrolling = ImVec2(0.0f, 0.0f);
		float nodeSlotRadius = 5.0f;
		float gridSize = 32.0f;
		bool showGrid = true;

		// Interaction state
		int selectedNodeId = -1;
		int hoveredNodeId = -1;
		int hoveredPinId = -1;
		bool isDraggingNode = false;
		bool isDraggingCanvas = false;
		bool isDraggingConnection = false;

		// Connection dragging
		int dragConnectionStart = -1;
		int dragConnectionPin = -1;
		ImVec2 dragConnectionEnd;

		// Graph
		std::unique_ptr<ShaderGraph> graph;

		// Generated shader code
		std::string vertexShaderCode;
		std::string fragmentShaderCode;
		std::string lastError;

		// Style
		std::unordered_map<PinType, PinStyle> pinStyles;

		// Internal methods
		void DrawGrid(ImDrawList* drawList, const ImVec2& canvasPos, const ImVec2& canvasSize);
		void DrawNodes(ImDrawList* drawList, const ImVec2& canvasPos);
		void DrawConnections(ImDrawList* drawList, const ImVec2& canvasPos);
		void DrawNode(ImDrawList* drawList, ShaderNode* node, const ImVec2& offset);
		void DrawNodePin(ImDrawList* drawList, const NodePin& pin, const ImVec2& pinPos);

		void HandleNodeInteraction(ShaderNode* node, const ImVec2& nodeRectMin);
		void HandlePinInteraction(ShaderNode* node, NodePin& pin, const ImVec2& pinPos);
		void HandleCanvasInteraction(const ImVec2& canvasPos, const ImVec2& canvasSize);
		void HandleContextMenu(const ImVec2& canvasPos);

		ImVec2 GetPinPosition(const ShaderNode* node, const NodePin& pin, const ImVec2& offset) const;
		ImU32 GetPinColor(PinType type) const;

		void CreateNode(const std::string& nodeType, const ImVec2& position);
		void DeleteSelectedNode();

		// Bezier curve for connections
		void DrawBezierConnection(ImDrawList* drawList, const ImVec2& p1, const ImVec2& p2, ImU32 color, float thickness = 2.0f);
	};
}