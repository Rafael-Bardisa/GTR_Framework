#include "scene.h"
#include "utils.h"
#include "fbo.h"
#include "prefab.h"
#include "extra/cJSON.h"

GTR::Scene* GTR::Scene::instance = NULL;

GTR::Scene::Scene()
{
	instance = this;
	
}

void GTR::Scene::clear()
{
	for (int i = 0; i < entities.size(); ++i)
	{
		BaseEntity* ent = entities[i];
		delete ent;
	}
	entities.resize(0);
}


void GTR::Scene::addEntity(BaseEntity* entity)
{
	entities.push_back(entity); entity->scene = this;
}

bool GTR::Scene::load(const char* filename)
{
	std::string content;

	this->filename = filename;
	std::cout << " + Reading scene JSON: " << filename << "..." << std::endl;

	if (!readFile(filename, content))
	{
		std::cout << "- ERROR: Scene file not found: " << filename << std::endl;
		return false;
	}

	//parse json string 
	cJSON* json = cJSON_Parse(content.c_str());
	if (!json)
	{
		std::cout << "ERROR: Scene JSON has errors: " << filename << std::endl;
		return false;
	}

	//read global properties
	background_color = readJSONVector3(json, "background_color", background_color);
	ambient_light = readJSONVector3(json, "ambient_light", ambient_light );
	main_camera.eye = readJSONVector3(json, "camera_position", main_camera.eye);
	main_camera.center = readJSONVector3(json, "camera_target", main_camera.center);
	main_camera.fov = readJSONNumber(json, "camera_fov", main_camera.fov);

	//entities
	cJSON* entities_json = cJSON_GetObjectItemCaseSensitive(json, "entities");
	cJSON* entity_json;
	cJSON_ArrayForEach(entity_json, entities_json)
	{
		std::string type_str = cJSON_GetObjectItem(entity_json, "type")->valuestring;
		BaseEntity* ent = createEntity(type_str);
		if (!ent)
		{
			std::cout << " - ENTITY TYPE UNKNOWN: " << type_str << std::endl;
			//continue;
			ent = new BaseEntity();
		}

		addEntity(ent);

		if (cJSON_GetObjectItem(entity_json, "name"))
		{
			ent->name = cJSON_GetObjectItem(entity_json, "name")->valuestring;
			stdlog(std::string(" + entity: ") + ent->name);
		}

		//read transform
		if (cJSON_GetObjectItem(entity_json, "position"))
		{
			ent->model.setIdentity();
			Vector3 position = readJSONVector3(entity_json, "position", Vector3());
			ent->model.translate(position.x, position.y, position.z);
		}

		if (cJSON_GetObjectItem(entity_json, "angle"))
		{
			float angle = cJSON_GetObjectItem(entity_json, "angle")->valuedouble;
			ent->model.rotate(angle * DEG2RAD, Vector3(0, 1, 0));
		}

		if (cJSON_GetObjectItem(entity_json, "rotation"))
		{
			Vector4 rotation = readJSONVector4(entity_json, "rotation");
			Quaternion q(rotation.x, rotation.y, rotation.z, rotation.w);
			Matrix44 R;
			q.toMatrix(R);
			ent->model = R * ent->model;
		}

		if (cJSON_GetObjectItem(entity_json, "target"))
		{
			Vector3 target = readJSONVector3(entity_json, "target", Vector3());
			Vector3 front = target - ent->model.getTranslation();
			ent->model.setFrontAndOrthonormalize(front);
		}

		if (cJSON_GetObjectItem(entity_json, "scale"))
		{
			Vector3 scale = readJSONVector3(entity_json, "scale", Vector3(1, 1, 1));
			ent->model.scale(scale.x, scale.y, scale.z);
		}

		ent->configure(entity_json);
	}

	//free memory
	cJSON_Delete(json);

	return true;
}

GTR::BaseEntity* GTR::Scene::createEntity(std::string type)
{
	if (type == "PREFAB")
		return new GTR::PrefabEntity();
    
    else if (type == "LIGHT")
        return new GTR::LightEntity();
    
    return NULL;
}

void GTR::BaseEntity::renderInMenu()
{
#ifndef SKIP_IMGUI
	ImGui::Text("Name: %s", name.c_str()); // Edit 3 floats representing a color
	ImGui::Checkbox("Visible", &visible); // Edit 3 floats representing a color
	//Model edit
	ImGuiMatrix44(model, "Model");
#endif
}




GTR::PrefabEntity::PrefabEntity()
{
	entity_type = PREFAB;
	prefab = NULL;
}



GTR::LightEntity::LightEntity()
{
    //default values
    entity_type = LIGHT;
    color = Vector3(1,1,1);
    intensity = 1;
    angle = 0;
    target = Vector3(1, 1, 1);
    
    area_size = 100;
    
    max_dist = 100;
    cone_angle = 30;
    cone_exp = 60;
    
    cast_shadows = false;
    shadow_bias = 0;
    
    light_camera = nullptr;
    
    atlas_shadowmap_dimensions = Vector4();
}

// configure the light camera to match light specs
void GTR::LightEntity::configCamera()
{
    if (type == DIRECTIONAL){
        //use light area to define how big the frustum is
        float halfarea = area_size / 2;
        // will be square because shadowmap
        light_camera->setOrthographic( -halfarea, halfarea, halfarea, -halfarea, 0.1, max_dist);
        light_camera->lookAt(model * Vector3(), model * Vector3(0,0,-1), model.rotateVector(Vector3(0,1,0)));

    }
    else {
        light_camera->setPerspective(cone_angle*2, 1.0, 0.1, max_dist);
        light_camera->lookAt(model * Vector3(), model * Vector3(0,0,-1), model.rotateVector(Vector3(0,1,0)));
    }
}

bool GTR::LightEntity::boxInFrustum(BoundingBox aabb){
    switch(type){
        case UNKNOWN:
            return false;
        case POINT:
            //box against sphere
            return true;
        case SPOT:
            //box against frustum
            return true;
        case DIRECTIONAL:
            //affects every object -> if true, would not filter any light
            return true;
        default:
            return false;
}
}

void GTR::PrefabEntity::configure(cJSON* json)
{
	if (cJSON_GetObjectItem(json, "filename"))
	{
		filename = cJSON_GetObjectItem(json, "filename")->valuestring;
		prefab = GTR::Prefab::Get( (std::string("data/") + filename).c_str());
	}
}

void GTR::PrefabEntity::renderInMenu()
{
	BaseEntity::renderInMenu();

#ifndef SKIP_IMGUI
	ImGui::Text("filename: %s", filename.c_str()); // Edit 3 floats representing a color
	if (prefab && ImGui::TreeNode(prefab, "Prefab Info"))
	{
		prefab->root.renderInMenu();
		ImGui::TreePop();
	}
#endif
}

void GTR::LightEntity::configure(cJSON* json)
{

    std::string type_str = readJSONString(json, "light_type", "");
    
    if (type_str == "SPOT"){
        type = SPOT;
    }
    else if (type_str == "POINT"){
        type = POINT;
    }
    else if (type_str == "DIRECTIONAL"){
        type =  DIRECTIONAL;
    }
    else {
        type = UNKNOWN;
    }
    
    this->color = readJSONVector3(json, "color", color);
    this->intensity = readJSONNumber(json, "intensity", intensity);
    this->angle = readJSONNumber(json, "angle", angle);
    
    this->area_size = readJSONNumber(json, "area_size", area_size);
    
    this->max_dist = readJSONNumber(json, "max_dist", max_dist);
    this->cone_angle = readJSONNumber(json, "cone_angle", cone_angle);
    this->cone_exp = readJSONNumber(json, "cone_exp", cone_exp);
    
    this->target = readJSONVector3(json, "target", target);
    
    this-> cast_shadows = readJSONBool(json, "cast_shadows", false);
    this->shadow_bias = readJSONNumber(json, "shadow_bias", shadow_bias);
    
    /*
     "color":[1,0.9,0.8],
     "intensity":10,
     "max_dist":1000,
     "cone_angle":45,
     "cone_exp":60,
     "light_type":"SPOT",
     "cast_shadows": true,
     "shadow_bias": 0.001
     */

}

void GTR::LightEntity::renderInMenu()
{
    BaseEntity::renderInMenu();
}
