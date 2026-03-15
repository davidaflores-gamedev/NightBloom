
#include "ShaderNodeEditor.hpp"
#include <sstream>
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <unordered_map>
#include <functional>

namespace Nightbloom
{
	// ============================================================================
	// ShaderGraph
	// ============================================================================

	ShaderGraph::ShaderGraph() {
		// Add default output node
		auto outputNode = std::make_unique<FragmentOutputNode>(nextNodeId++);
		outputNode->position = ImVec2(400, 200);
		AddNode(std::move(outputNode));
	}

	void ShaderGraph::AddNode(std::unique_ptr<ShaderNode> node) {
		nodes.push_back(std::move(node));
	}

	void ShaderGraph::RemoveNode(int nodeId) {
		// Remove connections to/from this node
		connections.erase(
			std::remove_if(connections.begin(), connections.end(),
				[nodeId](const NodeConnection& conn) {
					return conn.startNode == nodeId || conn.endNode == nodeId;
				}),
			connections.end()
		);

		// Remove the node
		nodes.erase(
			std::remove_if(nodes.begin(), nodes.end(),
				[nodeId](const std::unique_ptr<ShaderNode>& node) {
					return node->id == nodeId;
				}),
			nodes.end()
		);
	}

	ShaderNode* ShaderGraph::GetNode(int nodeId) {
		for (auto& node : nodes) {
			if (node->id == nodeId) {
				return node.get();
			}
		}
		return nullptr;
	}

	const ShaderNode* ShaderGraph::GetNode(int nodeId) const {
		for (const auto& node : nodes) {
			if (node->id == nodeId) {
				return node.get();
			}
		}
		return nullptr;
	}

	bool ShaderGraph::CanConnect(int startNode, int startPin, int endNode, int endPin) const {
		// Can't connect to self
		if (startNode == endNode) return false;

		// Check if connection already exists
		for (const auto& conn : connections) {
			if (conn.startNode == startNode && conn.startPin == startPin &&
				conn.endNode == endNode && conn.endPin == endPin) {
				return false;
			}
		}

		// TODO: Add type checking, cycle detection
		return true;
	}

	void ShaderGraph::Connect(int startNode, int startPin, int endNode, int endPin) {
		if (!CanConnect(startNode, startPin, endNode, endPin)) return;

		NodeConnection conn;
		conn.id = nextConnectionId++;
		conn.startNode = startNode;
		conn.startPin = startPin;
		conn.endNode = endNode;
		conn.endPin = endPin;

		connections.push_back(conn);

		// Update pin connection info
		if (ShaderNode* start = GetNode(startNode)) {
			if (startPin < start->outputPins.size()) {
				start->outputPins[startPin].connections.push_back(conn.id);
			}
		}

		if (ShaderNode* end = GetNode(endNode)) {
			if (endPin < end->inputPins.size()) {
				end->inputPins[endPin].connectedTo = conn.id;
			}
		}
	}

	void ShaderGraph::Disconnect(int connectionId) {
		auto it = std::find_if(connections.begin(), connections.end(),
			[connectionId](const NodeConnection& conn) {
				return conn.id == connectionId;
			});

		if (it != connections.end()) {
			// Clear pin references
			if (ShaderNode* start = GetNode(it->startNode)) {
				if (it->startPin < start->outputPins.size()) {
					auto& conns = start->outputPins[it->startPin].connections;
					conns.erase(std::remove(conns.begin(), conns.end(), connectionId), conns.end());
				}
			}

			if (ShaderNode* end = GetNode(it->endNode)) {
				if (it->endPin < end->inputPins.size()) {
					end->inputPins[it->endPin].connectedTo = -1;
				}
			}

			connections.erase(it);
		}
	}

	// UpdateInfo Must be ran before generating shader code
	std::string ShaderGraph::GenerateFragmentShader() const {
		std::stringstream ss;

		ss << "#version 450\n\n";




		if (usesTextures)
		{
			ss << "layout(set = 1, binding = 0) uniform sampler2D diffuseTexture;\n\n";
		}

		ss << "layout(location = 0) in vec3 fragColor;\n";
		ss << "layout(location = 1) in vec2 fragTexCoord;\n\n";
		ss << "layout(location = 0) out vec4 outColor;\n\n";

				// Uniform buffer for frame data
		ss << "layout(set = 0, binding = 0) uniform FrameUniforms {\n";
		ss << "    mat4 view;\n";
		ss << "    mat4 proj;\n";
		ss << "    vec4 time;\n";
		ss << "	   vec4 cameraPos;\n";
		ss << "} frame;\n\n";

		// Push constants for per-object data
		ss << "layout(push_constant) uniform PushConstants {\n";
		ss << "    mat4 model;\n";
		ss << "    vec4 customData;\n";
		ss << "} push;\n\n";
		
		ss << "void main() {\n";
		ss << "    float time = frame.time.x;\n\n";

		// Get nodes in dependency order
		std::vector<int> sortedNodeIds = GetTopologicalSort();

		// Generate code in correct order
		for (int nodeId : sortedNodeIds) {
			ShaderNode* node = const_cast<ShaderNode*>(GetNode(nodeId));
			if (node && dynamic_cast<FragmentOutputNode*>(node) == nullptr) {
				ss << "    // " << node->name << " (Node " << node->id << ")\n";
				ss << "    " << node->GenerateGLSL(this) << "\n";
			}
		}

		// Generate output node last
		for (const auto& node : nodes) {
			if (dynamic_cast<FragmentOutputNode*>(node.get()) != nullptr) {
				ss << "    // Output\n";
				ss << "    " << node->GenerateGLSL(this);
			}
		}

		ss << "}\n";
		return ss.str();
	}

	std::vector<int> ShaderGraph::GetTopologicalSort() const {
		std::vector<int> result;
		std::unordered_set<int> visited;
		std::unordered_set<int> visiting;

		// Build adjacency list (what nodes does each node depend on)
		std::unordered_map<int, std::vector<int>> dependencies;
		for (const auto& conn : connections) {
			dependencies[conn.endNode].push_back(conn.startNode);
		}

		// DFS helper
		std::function<void(int)> visit = [&](int nodeId) {
			if (visited.count(nodeId)) return;
			if (visiting.count(nodeId)) {
				// Cycle detected - shouldn't happen with proper UI
				return;
			}

			visiting.insert(nodeId);

			// Visit dependencies first
			if (dependencies.count(nodeId)) {
				for (int dep : dependencies[nodeId]) {
					visit(dep);
				}
			}

			visiting.erase(nodeId);
			visited.insert(nodeId);
			result.push_back(nodeId);
			};

		// Visit all nodes
		for (const auto& node : nodes) {
			visit(node->id);
		}

		return result;
	}

	std::string ShaderGraph::GenerateVertexShader() const {
		// For now, use a standard vertex shader
		std::stringstream ss;
		ss << "#version 450\n\n";

		// Uniform buffer for frame data
		ss << "layout(set = 0, binding = 0) uniform FrameUniforms {\n";
		ss << "    mat4 view;\n";
		ss << "    mat4 proj;\n";
		ss << "    vec4 time;\n";
		ss << "	   vec4 cameraPos;\n";
		ss << "} frame;\n\n";

		// Push constants for per-object data
		ss << "layout(push_constant) uniform PushConstants {\n";
		ss << "    mat4 model;\n";
		ss << "    vec4 customData;\n";
		ss << "} push;\n\n";

		ss << "layout(location = 0) in vec3 inPosition;\n";
		ss << "layout(location = 1) in vec3 inColor;\n";
		ss << "layout(location = 2) in vec2 inTexCoord;\n\n";
		ss << "layout(location = 0) out vec3 fragColor;\n";
		ss << "layout(location = 1) out vec2 fragTexCoord;\n\n";

		ss << "void main() {\n";
		ss << "    gl_Position = frame.proj * frame.view * push.model * vec4(inPosition, 1.0);\n";
		ss << "    fragColor = inColor;\n";
		ss << "    fragTexCoord = inTexCoord;\n";
		ss << "}\n";

		return ss.str();
	}

	void ShaderGraph::RefreshShaderInfo()
	{
		usesTextures = false;
		for (const auto& node : nodes) {
			if (dynamic_cast<TextureNode*>(node.get()) != nullptr) {
				usesTextures = true;
				
				for (auto& pin : node->outputPins) {
					pin.resolvedType = pin.type;
				}
			}
		}

		ResolveAllTypes();
	}

	void ShaderGraph::ResolveAllTypes()
	{
		// Multiple passes to propagate types through the graph
		bool changed = true;
		int passes = 0;
		while (changed && passes < 10) {
			changed = false;
			for (auto& node : nodes) {
				PinType oldType = node->outputPins.empty() ?
					PinType::Float : node->outputPins[0].resolvedType;

				node->ResolveTypes(this);

				if (!node->outputPins.empty() &&
					node->outputPins[0].resolvedType != oldType) {
					changed = true;
				}
			}
			passes++;
		}
	}

	// ============================================================================
	// ShaderNodeEditor - Main Editor Implementation
	// ============================================================================

	ShaderNodeEditor::ShaderNodeEditor() {
		graph = std::make_unique<ShaderGraph>();

		// Setup pin styles
		pinStyles[PinType::Float] = { IM_COL32(150, 150, 255, 255), 5.0f };
		pinStyles[PinType::Vec2] = { IM_COL32(150, 255, 150, 255), 5.0f };
		pinStyles[PinType::Vec3] = { IM_COL32(255, 150, 150, 255), 5.0f };
		pinStyles[PinType::Vec4] = { IM_COL32(255, 255, 150, 255), 5.0f };
		pinStyles[PinType::Texture2D] = { IM_COL32(255, 150, 255, 255), 5.0f };
		pinStyles[PinType::Matrix4] = { IM_COL32(150, 255, 255, 255), 5.0f };
	}

	ShaderNodeEditor::~ShaderNodeEditor() = default;

	void ShaderNodeEditor::Draw(const char* title, bool* p_open) {

		// Give the window a sensible first-time size and a hard minimum.
		ImGui::SetNextWindowSize(ImVec2(720, 480), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSizeConstraints(ImVec2(200, 150), ImVec2(FLT_MAX, FLT_MAX));

		// Request a menubar
		bool window_visible = ImGui::Begin(title, p_open, ImGuiWindowFlags_MenuBar);

		// If the window is collapsed/hidden this frame, skip all content but still End().
		if (!window_visible) {
			ImGui::End();
			return;
		}

		// Menu bar
		if (ImGui::BeginMenuBar()) {
			if (ImGui::BeginMenu("Add Node")) {
				if (ImGui::MenuItem("Color")) CreateNode("Color", ImGui::GetMousePos());
				if (ImGui::MenuItem("Texture")) CreateNode("Texture", ImGui::GetMousePos());
				if (ImGui::MenuItem("Time")) CreateNode("Time", ImGui::GetMousePos());
				if (ImGui::MenuItem("Multiply")) CreateNode("Multiply", ImGui::GetMousePos());
				if (ImGui::MenuItem("Add")) CreateNode("Add", ImGui::GetMousePos());
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("View")) {
				ImGui::Checkbox("Show Grid", &showGrid);
				ImGui::EndMenu();
			}
			ImGui::EndMenuBar();
		}

		// Canvas
		ImVec2 canvasPos = ImGui::GetCursorScreenPos();
		ImVec2 canvasSize = ImGui::GetContentRegionAvail();

		// Guard against zero/near-zero sizes (docking edges, first frame, tiny split).
		const float kMinW = 32.0f, kMinH = 32.0f;
		if (canvasSize.x < kMinW || canvasSize.y < kMinH) {
			ImGui::TextUnformatted("Expand this window to use the graph.");
			ImGui::End();
			return;
		}

		// Create canvas region
		ImGui::InvisibleButton("canvas", canvasSize,
			ImGuiButtonFlags_MouseButtonLeft |
			ImGuiButtonFlags_MouseButtonRight |
			ImGuiButtonFlags_MouseButtonMiddle);


		const bool isHovered = ImGui::IsItemHovered();
		const bool isActive = ImGui::IsItemActive();
		ImVec2 canvasMin = ImGui::GetItemRectMin();
		ImVec2 canvasMax = ImGui::GetItemRectMax();
		const ImVec2 mousePos = ImGui::GetMousePos();

		ImDrawList* drawList = ImGui::GetWindowDrawList();

		// Canvas background
		drawList->AddRectFilled(canvasMin, canvasMax, IM_COL32(40, 40, 50, 255));

		// Draw grid
		if (showGrid) DrawGrid(drawList, canvasMin, ImVec2(canvasMax.x - canvasMin.x, canvasMax.y - canvasMin.y));

		// Push clip rect for canvas
		drawList->PushClipRect(canvasMin, canvasMax, true);

		// Draw connections
		DrawConnections(drawList, canvasPos);

		// Draw nodes
		DrawNodes(drawList, canvasPos);

		// Handle canvas interaction
		HandleCanvasInteraction(canvasPos, canvasSize);

		// Draw connection being dragged
		if (isDraggingConnection) {
			ImVec2 p1 = dragConnectionEnd;
			if (dragConnectionStart >= 0 && dragConnectionPin >= 0) {
				if (ShaderNode* node = graph->GetNode(dragConnectionStart)) {
					if (dragConnectionPin < node->outputPins.size()) {
						p1 = GetPinPosition(node, node->outputPins[dragConnectionPin], canvasPos);
					}
				}
			}
			DrawBezierConnection(drawList, p1, mousePos, IM_COL32(200, 200, 100, 255), 2.0f);
		}

		drawList->PopClipRect();

		// Context menu
		if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
			ImGui::OpenPopup("context_menu");
		}

		HandleContextMenu(canvasPos);

		ImGui::End();
	}

	void ShaderNodeEditor::DrawGrid(ImDrawList* drawList, const ImVec2& canvasPos, const ImVec2& canvasSize) {
		const float GRID_SZ = gridSize;
		for (float x = fmodf(scrolling.x, GRID_SZ); x < canvasSize.x; x += GRID_SZ) {
			drawList->AddLine(ImVec2(canvasPos.x + x, canvasPos.y),
				ImVec2(canvasPos.x + x, canvasPos.y + canvasSize.y),
				IM_COL32(200, 200, 200, 40));
		}
		for (float y = fmodf(scrolling.y, GRID_SZ); y < canvasSize.y; y += GRID_SZ) {
			drawList->AddLine(ImVec2(canvasPos.x, canvasPos.y + y),
				ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + y),
				IM_COL32(200, 200, 200, 40));
		}
	}

	void ShaderNodeEditor::DrawNodes(ImDrawList* drawList, const ImVec2& canvasPos) {
		for (auto& node : graph->nodes) {
			DrawNode(drawList, node.get(), canvasPos);
		}
	}

	void ShaderNodeEditor::DrawNode(ImDrawList* drawList, ShaderNode* node, const ImVec2& offset) {
		ImGui::PushID(node->id);

		ImVec2 nodeRectMin = ImVec2(offset.x + node->position.x + scrolling.x,
			offset.y + node->position.y + scrolling.y);
		ImVec2 nodeRectMax = ImVec2(nodeRectMin.x + node->size.x, nodeRectMin.y + node->size.y);

		// Node background
		ImU32 nodeBg = (node->id == selectedNodeId) ? IM_COL32(75, 75, 140, 255) : IM_COL32(50, 50, 50, 255);
		drawList->AddRectFilled(nodeRectMin, nodeRectMax, nodeBg, 4.0f);
		drawList->AddRect(nodeRectMin, nodeRectMax, IM_COL32(100, 100, 100, 255), 4.0f);

		// Title bar
		ImVec2 titleBarMax = ImVec2(nodeRectMax.x, nodeRectMin.y + 30);
		drawList->AddRectFilled(nodeRectMin, titleBarMax, IM_COL32(70, 70, 100, 255), 4.0f, ImDrawFlags_RoundCornersTop);

		// Title text
		ImVec2 textPos = ImVec2(nodeRectMin.x + 10, nodeRectMin.y + 8);
		drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), node->name.c_str());

		// Draw pins
		const float pinOffset = 40.0f;
		float yPos = nodeRectMin.y + pinOffset;

		// Input pins (left side)
		for (auto& pin : node->inputPins) {
			ImVec2 pinPos = ImVec2(nodeRectMin.x, yPos);
			pin.pos = pinPos;

			DrawNodePin(drawList, pin, pinPos);

			// Pin label
			drawList->AddText(ImVec2(pinPos.x + 15, pinPos.y - 8),
				IM_COL32(200, 200, 200, 255), pin.name.c_str());

			yPos += 25.0f;
		}

		// Output pins (right side)
		yPos = nodeRectMin.y + pinOffset;
		for (auto& pin : node->outputPins) {
			ImVec2 pinPos = ImVec2(nodeRectMax.x, yPos);
			pin.pos = pinPos;

			DrawNodePin(drawList, pin, pinPos);

			// Pin label (right-aligned)
			ImVec2 textSize = ImGui::CalcTextSize(pin.name.c_str());
			drawList->AddText(ImVec2(pinPos.x - textSize.x - 15, pinPos.y - 8),
				IM_COL32(200, 200, 200, 255), pin.name.c_str());

			yPos += 25.0f;
		}

		// Handle interaction
		HandleNodeInteraction(node, nodeRectMin);

		// Handle pin interactions
		for (auto& pin : node->inputPins) {
			HandlePinInteraction(node, pin, pin.pos);
		}
		for (auto& pin : node->outputPins) {
			HandlePinInteraction(node, pin, pin.pos);
		}

		ImGui::PopID();
	}

	void ShaderNodeEditor::DrawNodePin(ImDrawList* drawList, const NodePin& pin, const ImVec2& pinPos) {
		ImU32 pinColor;

		// Show resolved type color if available, otherwise show gray for "any"
		if (pin.resolvedType != PinType::Any) {
			pinColor = GetPinColor(pin.resolvedType);
		}
		else if (pin.type != PinType::Any) {
			pinColor = GetPinColor(pin.type);
		}
		else {
			pinColor = IM_COL32(128, 128, 128, 255); // Gray for unresolved
		}

		float radius = nodeSlotRadius;
		bool isConnected = pin.isInput ? (pin.connectedTo >= 0) : (!pin.connections.empty());

		if (isConnected) {
			drawList->AddCircleFilled(pinPos, radius, pinColor);
		}
		else {
			drawList->AddCircle(pinPos, radius, pinColor, 12, 2.0f);
		}
	}

	void ShaderNodeEditor::DrawConnections(ImDrawList* drawList, const ImVec2& canvasPos) {
		for (const auto& conn : graph->connections) {
			ShaderNode* startNode = graph->GetNode(conn.startNode);
			ShaderNode* endNode = graph->GetNode(conn.endNode);

			if (!startNode || !endNode) continue;
			if (conn.startPin >= startNode->outputPins.size()) continue;
			if (conn.endPin >= endNode->inputPins.size()) continue;

			ImVec2 p1 = GetPinPosition(startNode, startNode->outputPins[conn.startPin], canvasPos);
			ImVec2 p2 = GetPinPosition(endNode, endNode->inputPins[conn.endPin], canvasPos);

			ImU32 color = GetPinColor(startNode->outputPins[conn.startPin].type);
			DrawBezierConnection(drawList, p1, p2, color);
		}
	}

	void ShaderNodeEditor::DrawBezierConnection(ImDrawList* drawList, const ImVec2& p1, const ImVec2& p2, ImU32 color, float thickness) {
		float dist = fabs(p2.x - p1.x) * 0.5f;
		ImVec2 cp1 = ImVec2(p1.x + dist, p1.y);
		ImVec2 cp2 = ImVec2(p2.x - dist, p2.y);

		drawList->AddBezierCubic(p1, cp1, cp2, p2, color, thickness);
	}

	ImVec2 ShaderNodeEditor::GetPinPosition(const ShaderNode* node, const NodePin& pin, const ImVec2& offset) const {
		return ImVec2(offset.x + node->position.x + scrolling.x + (pin.isInput ? 0 : node->size.x),
			offset.y + node->position.y + scrolling.y + 40.0f + pin.pinId * 25.0f);
	}

	ImU32 ShaderNodeEditor::GetPinColor(PinType type) const {
		auto it = pinStyles.find(type);
		if (it != pinStyles.end()) {
			return it->second.color;
		}
		return IM_COL32(255, 255, 255, 255);
	}

	void ShaderNodeEditor::HandleNodeInteraction(ShaderNode* node, const ImVec2& nodeRectMin) {
		ImVec2 nodeRectMax = ImVec2(nodeRectMin.x + node->size.x, nodeRectMin.y + node->size.y);

		// Check if mouse is over node
		if (ImGui::IsMouseHoveringRect(nodeRectMin, nodeRectMax)) {
			hoveredNodeId = node->id;

			// Select on click
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				selectedNodeId = node->id;
				isDraggingNode = true;
			}

			// Open properties on double-click
			if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
				ImGui::OpenPopup("node_properties");
			}
		}

		// Drag node
		if (isDraggingNode && selectedNodeId == node->id) {
			if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
				node->position.x += ImGui::GetIO().MouseDelta.x;
				node->position.y += ImGui::GetIO().MouseDelta.y;
			}
			if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
				isDraggingNode = false;
			}
		}

		// Properties popup
		if (ImGui::BeginPopup("node_properties")) {
			ImGui::Text("Node: %s", node->name.c_str());
			ImGui::Separator();
			node->DrawProperties();
			ImGui::EndPopup();
		}
	}

	void ShaderNodeEditor::HandlePinInteraction(ShaderNode* node, NodePin& pin, const ImVec2& pinPos) {
		float radius = nodeSlotRadius * 1.5f;

		if (ImGui::IsMouseHoveringRect(ImVec2(pinPos.x - radius, pinPos.y - radius),
			ImVec2(pinPos.x + radius, pinPos.y + radius))) {
			hoveredPinId = pin.pinId;

			// Start connection drag from output pin
			if (!pin.isInput && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				isDraggingConnection = true;
				dragConnectionStart = node->id;
				dragConnectionPin = pin.pinId;
				dragConnectionEnd = pinPos;
			}

			// Complete connection to input pin
			if (pin.isInput && isDraggingConnection && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
				if (dragConnectionStart >= 0 && dragConnectionPin >= 0) {
					graph->Connect(dragConnectionStart, dragConnectionPin, node->id, pin.pinId);
				}
				isDraggingConnection = false;
				dragConnectionStart = -1;
				dragConnectionPin = -1;
			}
		}
	}

	void ShaderNodeEditor::HandleCanvasInteraction(const ImVec2& canvasPos, const ImVec2& canvasSize) {
		const bool isHovered = ImGui::IsMouseHoveringRect(canvasPos,
			ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y));

		// Pan with middle mouse
		if (isHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
			scrolling.x += ImGui::GetIO().MouseDelta.x;
			scrolling.y += ImGui::GetIO().MouseDelta.y;
		}

		// Cancel connection drag
		if (isDraggingConnection && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
			isDraggingConnection = false;
			dragConnectionStart = -1;
			dragConnectionPin = -1;
		}

		// Delete selected node
		if (selectedNodeId >= 0 && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
			DeleteSelectedNode();
		}
	}

	void ShaderNodeEditor::HandleContextMenu(const ImVec2& canvasPos) {
		if (ImGui::BeginPopup("context_menu")) {
			ImVec2 mousePos = ImGui::GetMousePos();
			ImVec2 nodePos = ImVec2(mousePos.x - canvasPos.x - scrolling.x,
				mousePos.y - canvasPos.y - scrolling.y);

			if (ImGui::MenuItem("Add Color Node")) {
				CreateNode("Color", nodePos);
			}
			if (ImGui::MenuItem("Add Texture Node")) {
				CreateNode("Texture", nodePos);
			}
			if (ImGui::MenuItem("Add Time Node")) {
				CreateNode("Time", nodePos);
			}
			if (ImGui::MenuItem("Add Multiply Node")) {
				CreateNode("Multiply", nodePos);
			}
			if (ImGui::MenuItem("Add Add Node")) {
				CreateNode("Add", nodePos);
			}
			if (ImGui::MenuItem("Add Mix Node")) {
				CreateNode("Mix", nodePos);
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Delete Selected", nullptr, false, selectedNodeId >= 0)) {
				DeleteSelectedNode();
			}

			ImGui::EndPopup();
		}
	}

	void ShaderNodeEditor::CreateNode(const std::string& nodeType, const ImVec2& position) {
		std::unique_ptr<ShaderNode> newNode;
		int id = graph->nextNodeId++;

		if (nodeType == "Color") {
			newNode = std::make_unique<ColorNode>(id);
		}
		else if (nodeType == "Texture") {
			newNode = std::make_unique<TextureNode>(id);
		}
		else if (nodeType == "Time") {
			newNode = std::make_unique<TimeNode>(id);
		}
		else if (nodeType == "Multiply") {
			newNode = std::make_unique<MultiplyNode>(id);
		}
		else if (nodeType == "Add") {
			newNode = std::make_unique<AddNode>(id);
		}
		else if (nodeType == "Mix") {
			newNode = std::make_unique<MixNode>(id);
		}

		if (newNode) {
			newNode->position = position;
			graph->AddNode(std::move(newNode));
		}
	}

	void ShaderNodeEditor::DeleteSelectedNode() {
		if (selectedNodeId > 0) { // Don't delete output node (id = 1)
			graph->RemoveNode(selectedNodeId);
			selectedNodeId = -1;
		}
	}

	bool ShaderNodeEditor::CompileShaders() {
		try {
			graph->RefreshShaderInfo();
			vertexShaderCode = graph->GenerateVertexShader();
			fragmentShaderCode = graph->GenerateFragmentShader();
			return true;
		}
		catch (const std::exception& e) {
			lastError = e.what();
			return false;
		}
	}
}