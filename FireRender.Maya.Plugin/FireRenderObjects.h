#pragma once
//
// Copyright (C) AMD
//
// File: FireRenderObjects.h
//
// Created by Alan Stanzione.
//

#include "frWrap.h"
#include "Translators.h"
#include <maya/MNodeMessage.h>
#include <maya/MDagMessage.h>
#include <maya/MPlug.h>
#include <string>
#include <atomic>
#include "FireMaya.h"

// Forward declarations
class FireRenderContext;
class SkyBuilder;

class HashValue
{
	const static size_t BigDumbPrime = 0x1fffffffffffffff;
	size_t value = 0;

	template<class T>
	size_t HashItems(const T* v, int count, size_t ret)
	{
		auto n = sizeof(T) * count;
		auto p = reinterpret_cast<const unsigned char*>(v);

		if (!p)
			return (ret >> 17 | ret << 47) ^ ((n + ret) * BigDumbPrime);

		for (int i = 0; i < n; i++)
			ret = (ret >> 17 | ret << 47) ^ ((p[i] + i + 1 + ret) * BigDumbPrime);

		return ret;
	}

public:
	HashValue(size_t v = 0) : value(v) {}

	bool operator==(const HashValue& h) const { return value == h.value; }
	bool operator!=(const HashValue& h) const { return value != h.value; }

	template <class T>
	HashValue& operator<<(const T& v)
	{
		value = HashItems(&v, 1, value);
		return *this;
	}

	template <class T>
	void Append(const T* v, int count)
	{
		value = HashItems(v, count, value);
	}

	operator size_t() const { return value; }
	operator int() const
	{
		return  int((value >> 32) ^ value);
	}
};


// FireRenderObject
// Base class for each translated object
class FireRenderObject
{

protected:

	struct
	{
		// Plug dirty flag
		// this is true when Maya is evaluating the plug dirty callback
		// used to avoid race conditions between the plug dirty and the attribute changed callbacks
		std::list<MCallbackId> callbackId;
		FireRenderContext* context = nullptr;
		std::string uuid;
		MObject		object;
		HashValue	hash;
		unsigned int instance = 0;
	} m;
public:

	// Constructor
	FireRenderObject(FireRenderContext* context, const MObject& object = MObject::kNullObj);

	// Destructor
	virtual ~FireRenderObject();

	// Clear
	virtual void clear();

	// uuid
	std::string uuid();

	// Set dirty
	void setDirty();

	static void Dump(const MObject& ob, int depth = 0, int maxDepth = 4);
	static HashValue GetHash(const MObject& ob);

	// update fire render objects using Maya objects, then marks as clean
	virtual void Freshen();

	// hash is generated during Freshen call
	HashValue GetStateHash() { return m.hash; }

	// Return the render context
	FireRenderContext* context() { return m.context; }

	frw::Scene		Scene();
	frw::Context	Context();
	FireMaya::Scope Scope();

	const MObject& Object() const { return m.object; }
	void SetObject(const MObject& ob);

	void AddCallback(MCallbackId id);
	void ClearCallbacks();
	virtual void RegisterCallbacks();

	template <class T>
	static T GetPlugValue(const MObject& ob, const char* name, T defaultValue)
	{
		MStatus mStatus;
		MFnDependencyNode node(ob, &mStatus);
		if (mStatus == MStatus::kSuccess)
		{
			auto displayPlug = node.findPlug(name);
			if (!displayPlug.isNull())
				displayPlug.getValue(defaultValue);
		}
		return defaultValue;
	}

	template <class T>
	T GetPlugValue(const char* name, T defaultValue)
	{
		return GetPlugValue(Object(), name, defaultValue);
	}

protected:
	// node dirty
	virtual void OnNodeDirty();
	static void NodeDirtyCallback(MObject& node, void* clientData);

	// attribute changed
	virtual void attributeChanged(MNodeMessage::AttributeMessage msg, MPlug &plug, MPlug &otherPlug) {}
	static void attributeChanged_callback(MNodeMessage::AttributeMessage msg, MPlug& plug, MPlug& otherPlug, void* clientData);
	static void attributeAddedOrRemoved_callback(MNodeMessage::AttributeMessage msg, MPlug& plug, void* clientData);

	// Plug dirty
	virtual void OnPlugDirty(MObject& node, MPlug &plug) {}
	static void plugDirty_callback(MObject& node, MPlug& plug, void* clientData);

	virtual HashValue CalculateHash();

public:
	bool m_isPortal_IBL;
	bool m_isPortal_SKY;
};

class FireRenderNode : public FireRenderObject
{
protected:
	bool m_isVisible = false;

public:
	// Constructor
	FireRenderNode(FireRenderContext* context, const MDagPath& dagPath);
	virtual ~FireRenderNode();

	// Detach from the fire render scene
	virtual void detachFromScene() {};

	// Attach to the fire render scene
	virtual void attachToScene() {};

	virtual bool IsEmissive() { return false; }

	// transform change callback
	// Plug dirty
	virtual void OnPlugDirty(MObject& node, MPlug &plug) override;
	virtual HashValue CalculateHash() override;

	virtual void OnWorldMatrixChanged();
	static void WorldMatrixChangedCallback(MObject& transformNode, MDagMessage::MatrixModifiedFlags& modified, void* clientData);

	virtual void RegisterCallbacks() override;

	bool IsVisible() { return m_isVisible; }

	std::vector<frw::Shape> GetVisiblePortals();

	MDagPath DagPath();
	unsigned int Instance() const { return m.instance; }


	// Record the state of a portal
	// node before it is used as a portal.
	void RecordPortalState(MObject node);

	// Restore portal nodes to their original
	// states before being used as portals.
	void RestorePortalStates();

	// Record the state of a portal
	// node before it is used as a portal.
	void RecordPortalState(MObject node, bool iblPortals);

	// Restore portal nodes to their original
	// states before being used as portals.
	void RestorePortalStates(bool iblPortals);

	// The list of currently visible portal
	// nodes and whether they were originally
	//shadow casters before being used as a portal.
	std::vector<std::pair<MObject, bool>> m_visiblePortals;

	std::vector<std::pair<MObject, bool>> m_visiblePortals_IBL;
	std::vector<std::pair<MObject, bool>> m_visiblePortals_SKY;
};

// Fire render mesh
// Bridge class between a Maya mesh node and a fr_shape
class FireRenderMesh : public FireRenderNode
{
public:

	// Constructor
	FireRenderMesh(FireRenderContext* context, const MDagPath& dagPath);

	// Destructor
	virtual ~FireRenderMesh();

		// Clear
	virtual void clear() override;
private:
	// Detach from the scene
	virtual void detachFromScene() override;

	// Attach to the scene
	virtual void attachToScene() override;
public:
	// Register the callback
	virtual void RegisterCallbacks() override;

	// transform attribute changed callback
	virtual void OnNodeDirty() override;

	// node dirty
	virtual void OnShaderDirty(MObject& node);
	static void ShaderDirtyCallback(MObject& node, void* clientData);


	virtual void Freshen() override;


	// build a sphere
	void buildSphere();

	void setRenderStats(MDagPath dagPath);
	void setVisibility(bool visibility);
	void setReflectionVisibility(bool reflectionVisibility);
	void setCastShadows(bool castShadow);
	void setPrimaryVisibility(bool primaryVisibility);

	virtual bool IsEmissive() override { return m.isEmissive; }

	void setupDisplacement(MObject shadingEngine, frw::Shape shape);
	void Rebuild();
	void RebuildTransforms();


	// Mesh bits (each one may have separate shading engine)
	std::vector<FrElement>& Elements() { return m.elements; }
	FrElement& Element(int i) { return m.elements[i]; }
private:

	// A mesh in Maya can have multiple shaders
	// in fr it must be split in multiple shapes
	// so this return the list of all the fr_shapes created for this Maya mesh

	struct
	{
		std::vector<FrElement> elements;
		bool isEmissive = false;
		struct
		{
			bool mesh = false;
			bool transform = false;
			bool shader = false;
		} changed;
	} m;

	virtual HashValue CalculateHash() override;
};


// Fire render light
// Bridge class between a Maya light node and a frw::Light
class FireRenderLight : public FireRenderNode
{
public:

	// Constructor
	FireRenderLight(FireRenderContext* context, const MDagPath& dagPath);

	// Destructor
	virtual ~FireRenderLight();

	// Return the fr light
	const FrLight& data() { return m_light; }

	// clear
	virtual void clear();

	// detach from the scene
	void detachFromScene() override;

	// attach to the scene
	void attachToScene() override;

	virtual bool IsEmissive() override { return true; }

	virtual void Freshen() override;

	// build light for swatch renderer
	void buildSwatchLight();

	// set portal
	void setPortal(bool value);

	// return portal
	bool portal();

private:

	// Transform matrix
	MMatrix m_matrix;

	// Fr light
	FrLight m_light;

	// portal flag
	bool m_portal;
};

// Fire render environment light
// Bridge class between a Maya environment light node and a frw::Light
class FireRenderEnvLight : public FireRenderNode
{
public:

	// Constructor
	FireRenderEnvLight(FireRenderContext* context, const MDagPath& dagPath);

	// Destructor
	virtual ~FireRenderEnvLight();

	// return the frw::Light
	const frw::Light& data();

	// clear
	virtual void clear() override;

	// detach from the scene
	virtual void detachFromScene() override;

	// attach to the scene
	virtual void attachToScene() override;

	virtual void Freshen() override;

	virtual bool IsEmissive() override { return true; }

	inline frw::EnvironmentLight getLight() { return m.light; }
private:

	// Transform matrix
	MMatrix m_matrix;

public:
	struct
	{
		frw::EnvironmentLight light;
		frw::EnvironmentLight bgOverride;

		frw::Image image;
	} m;
};

// Fire render camera
// Bridge class between a Maya camera node and a frw::Camera
class FireRenderCamera : public FireRenderNode
{
public:

	// Constructor
	FireRenderCamera(FireRenderContext* context, const MDagPath& dagPath);

	// Destructor
	virtual ~FireRenderCamera();

	// return the frw::Camera
	const frw::Camera& data();

	// clear
	virtual void clear();
	virtual void Freshen() override;

	// build camera for swatch render
	void buildSwatchCamera();

	// Set the camera type (default or a VR camera).
	void setType(short type);

	virtual void RegisterCallbacks() override;

private:

	// transform matrix
	MMatrix m_matrix;
	MObject m_imagePlane;

	// Camera type (default or a VR camera).
	short m_type;

	// Camera
	frw::Camera m_camera;
};


// Fire render display layer
// Maya display layer wrapper
class FireRenderDisplayLayer : public FireRenderObject
{
public:

	// Constructor
	FireRenderDisplayLayer(FireRenderContext* context, const MObject& object);

	// Destructor
	virtual ~FireRenderDisplayLayer();

	// clear
	virtual void clear();

	// attribute changed callback
	virtual void attributeChanged(MNodeMessage::AttributeMessage msg, MPlug &plug, MPlug &otherPlug) override;

};

// Fire render sky
// Bridge class between a Maya sky node and a frw::Light
class FireRenderSky : public FireRenderNode
{
public:

	// Constructor
	FireRenderSky() = delete;
	FireRenderSky(FireRenderContext* context, const MDagPath& dagPath);

	// Destructor
	virtual ~FireRenderSky();

	// Refresh the sky.
	virtual void Freshen() override;

	// clear
	virtual void clear();

	// detach from the scene
	virtual void detachFromScene();

	// attach to the scene
	virtual void attachToScene();

	// The sky system is emissive.
	virtual bool IsEmissive() override { return true; }

	// Attach portals.
	void attachPortals();

	// Detach portals.
	void detachPortals();

private:

	// transform matrix
	MMatrix m_matrix;

public:
	// The environment light.
	frw::EnvironmentLight m_envLight;

private:
	// The sun light.
	frw::DirectionalLight m_sunLight;

	// The environment image.
	frw::Image m_image;

	// Sky builder.
	SkyBuilder* m_skyBuilder;

	// This list of active portals.
	std::vector<frw::Shape> m_portals;

	// True once the sky has been initialized.
	bool m_initialized = false;
};

