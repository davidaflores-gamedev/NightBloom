//------------------------------------------------------------------------------
// Scene.hpp
//
// Minimal scene management - SceneObject wrapper and Scene container
// MVP implementation for editor hierarchy/inspector
//------------------------------------------------------------------------------
#pragma once

#include "Engine/Renderer/Model.hpp"
#include "Engine/Renderer/DrawCommandSystem.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>

namespace Nightbloom
{
	//--------------------------------------------------------------------------
	// SceneObject - Wrapper for anything that can exist in the scene
	//--------------------------------------------------------------------------
	struct SceneObject
	{
		std::string name;
		bool visible = true;

		// The actual renderable (we own the model, drawable is created from it)
		std::unique_ptr<Model> model;
		std::unique_ptr<ModelDrawable> drawable;

		// For obhects without a model
		std::unique_ptr<MeshDrawable> meshDrawable;

		// Track primitive state for UI sync
		int textureIndex = 0;        // Which texture is assigned (for UI display)
		PipelineType pipeline = PipelineType::Mesh;  // Which pipeline (for primitives)

		glm::mat4 primitiveTransform = glm::mat4(1.0f);

		// Convenience accessors for transform
		glm::vec3 GetPosition() const
		{
			return model ? model->GetPosition() : glm::vec3(0.0f);
		}

		glm::vec3 GetRotation() const
		{
			return model ? model->GetRotation() : glm::vec3(0.0f);
		}

		glm::vec3 GetScale() const
		{
			return model ? model->GetScale() : glm::vec3(1.0f);
		}

		void SetPosition(const glm::vec3& pos)
		{
			if (model) model->SetPosition(pos);
		}

		void SetRotation(const glm::vec3& rot)
		{
			if (model) model->SetRotation(rot);
		}

		void SetScale(const glm::vec3& scale)
		{
			if (model) model->SetScale(scale);
		}

		void SetScale(float uniform)
		{
			if (model) model->SetScale(uniform);
		}

		IDrawable* GetDrawable() const {
			if (drawable) return drawable.get();
			if (meshDrawable) return meshDrawable.get();
			return nullptr;
		}

		// Stats for inspector
		size_t GetMeshCount() const { return model ? model->GetMeshCount() : 0; }
		size_t GetVertexCount() const { return model ? model->GetTotalVertexCount() : 0; }
		size_t GetIndexCount() const { return model ? model->GetTotalIndexCount() : 0; }
	};

	//--------------------------------------------------------------------------
	// Scene - Container for SceneObjects with selection tracking
	//--------------------------------------------------------------------------
	class Scene
	{
	public:
		Scene() = default;
		~Scene() = default;

		// Add a model-based object to the scene
		SceneObject* AddObject(const std::string& name, std::unique_ptr<Model> model, Texture* defaultTexture = nullptr)
		{
			auto& obj = m_Objects.emplace_back();
			obj.name = name;
			obj.model = std::move(model);

			if (obj.model)
			{
				obj.drawable = std::make_unique<ModelDrawable>(obj.model.get(), defaultTexture);
			}

			return &m_Objects.back();
		}

		// Add a mesh drawable (for primitives like test cubes)
		SceneObject* AddPrimitive(const std::string& name, std::unique_ptr<MeshDrawable> meshDrawable)
		{
			auto& obj = m_Objects.emplace_back();
			obj.name = name;
			obj.meshDrawable = std::move(meshDrawable);
			return &m_Objects.back();
		}

		void Select(int index)
		{
			m_SelectedIndex = (index >= 0 && index < static_cast<int>(m_Objects.size()))
				? index : -1;
		}

		void Deselect() { m_SelectedIndex = -1; }

		int GetSelectedIndex() const { return m_SelectedIndex; }

		SceneObject* GetSelected()
		{
			if (m_SelectedIndex >= 0 && m_SelectedIndex < static_cast<int>(m_Objects.size()))
			{
				return &m_Objects[m_SelectedIndex];
			}
			return nullptr;
		}

		const SceneObject* GetSelected() const
		{
			if (m_SelectedIndex >= 0 && m_SelectedIndex < static_cast<int>(m_Objects.size()))
			{
				return &m_Objects[m_SelectedIndex];
			}
			return nullptr;
		}

		// Iteration
		std::vector<SceneObject>& GetObjects() { return m_Objects; }
		const std::vector<SceneObject>& GetObjects() const { return m_Objects; }
		size_t GetObjectCount() const { return m_Objects.size(); }

		SceneObject* GetObject(size_t index)
		{
			return index < m_Objects.size() ? &m_Objects[index] : nullptr;
		}

		// Build draw list from all visible objects
		void BuildDrawList(DrawList& drawList) const
		{
			for (const auto& obj : m_Objects)
			{
				if (obj.visible)
				{
					if (auto* drawable = obj.GetDrawable())
					{
						drawList.AddDrawable(drawable);
					}
				}
			}
		}

		void Update(float deltaTime)
		{
			for (auto& obj : m_Objects)
			{
				if (obj.drawable)
				{
					obj.drawable->Update(deltaTime);
				}
				if (obj.meshDrawable)
				{
					obj.meshDrawable->Update(deltaTime);
				}
			}
		}

		// Remove object by index
		void RemoveObject(size_t index)
		{
			if (index < m_Objects.size())
			{
				m_Objects.erase(m_Objects.begin() + index);

				// Adjust selection
				if (m_SelectedIndex == static_cast<int>(index))
				{
					m_SelectedIndex = -1;
				}
				else if (m_SelectedIndex > static_cast<int>(index))
				{
					m_SelectedIndex--;
				}
			}
		}

		void Clear()
		{
			m_Objects.clear();
			m_SelectedIndex = -1;
		}

	private:
		std::vector<SceneObject> m_Objects;
		int m_SelectedIndex = -1;
	};

} // namespace Nightbloom