#include "renderer.h"

#include "camera.h"
#include "shader.h"
#include "mesh.h"
#include "texture.h"
#include "prefab.h"
#include "material.h"
#include "utils.h"
#include "scene.h"
#include "extra/hdre.h"
#include <cmath>
#include <math.h>
#include "application.h"

using namespace GTR;

float renderFactor(GTR::eAlphaMode mode){
    return mode == GTR::eAlphaMode::BLEND ? 1.0f : -1.0f;
}

// inverse render priority: more negative number must be rendered first
// maybe overengineered, must test for errors
bool GTR::renderPriority(const RenderInstruct& first, const RenderInstruct& second){
    return ((renderFactor(first.material->alpha_mode) / first.distance) < (renderFactor(second.material->alpha_mode) / second.distance));
}

Renderer::Renderer(){
    num_lights = 0;
}
// upgraded this mofo
// ordered, something weird with colors before loading textures
void Renderer::renderScene(GTR::Scene* scene, Camera* camera)
{
    current_scene = scene;
	//set the clear color (the background color)
	glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);
    
    //empty instructions and lights list
    instructions.clear();
    lights.clear();

	// Clear the color and the depth buffer
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	checkGLErrors();


	for (int i = 0; i < scene->entities.size(); ++i)
	{
		BaseEntity* ent = scene->entities[i];
		if (!ent->visible)
			continue;

		//is a prefab!
		if (ent->entity_type == PREFAB)
		{
			PrefabEntity* pent = (GTR::PrefabEntity*)ent;
			if(pent->prefab)
				renderPrefab(ent->model, pent->prefab, camera);
		}
        else if (ent->entity_type == LIGHT){
            lights.push_back((GTR::LightEntity*)ent);
        }
	}
    num_lights = lights.size();
    // sort node vector by priority
    std::sort(instructions.begin(), instructions.end(), GTR::renderPriority);

    // render nodes by priority
    
    //each node rendered with all the lights
    for(auto instruction = instructions.begin(); instruction != instructions.end(); instruction++) {
        renderInstruction(*instruction, camera);
       }
}

//adds nodes of prefab to renderer node list
void Renderer::renderPrefab(const Matrix44& model, GTR::Prefab* prefab, Camera* camera)
{
	assert(prefab && "PREFAB IS NULL");
	//assign the model to the root node
	renderNode(model, &prefab->root, camera);
}

//adds a node, if visible, to the node vector and recursively calls on its children
void Renderer::renderNode(const Matrix44& prefab_model, GTR::Node* node, Camera* camera)
{
	if (!node->visible)
		return;

	//compute global matrix
	Matrix44 node_model = node->getGlobalMatrix(true) * prefab_model;

	//does this node have a mesh? then we must render it
	if (node->mesh && node->material)
	{
		//compute the bounding box of the object in world space (by using the mesh bounding box transformed to world space)
		BoundingBox world_bounding = transformBoundingBox(node_model,node->mesh->box);
		
		//if bounding box is inside the camera frustum then the object is probably visible
		if (camera->testBoxInFrustum(world_bounding.center, world_bounding.halfsize) )
		{
			//render node mesh
            // compare (a, b) is: a before b?

            // new RenderInstruct, must add to cool vector
            instructions.push_back(RenderInstruct( node_model, node->mesh, node->material, camera->eye.distance(world_bounding.center)));
			//node->mesh->renderBounding(node_model, true);
            
		}
	}

	//iterate recursively with children
	for (int i = 0; i < node->children.size(); ++i)
		renderNode(prefab_model, node->children[i], camera);
}

static void uploadCommonData(const GTR::Renderer &object, Camera *camera, GTR::Material *material, const Matrix44 &model, Shader *shader, Texture *color_texture, Texture *emissive_texture, Texture *metallic_texture, Texture *normal_texture, Texture *occlusion_texture) {
    shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
    shader->setUniform("u_camera_position", camera->eye);
    shader->setUniform("u_model", model );
    float t = getTime();
    shader->setUniform("u_time", t );
    
    shader->setUniform("u_color", material->color);
    if(color_texture)
        shader->setUniform("u_color_texture", color_texture, 0);
    
    if(emissive_texture)
        shader->setUniform("u_emissive_texture", emissive_texture, 1);
    if(metallic_texture)
        shader->setUniform("u_metallic_texture", metallic_texture, 2);
    if(normal_texture)
        shader->setUniform("u_normal_texture", normal_texture, 3);
    if(occlusion_texture)
        shader->setUniform("u_occlusion_texture", occlusion_texture, 4);
    
    
    //this is used to say which is the alpha threshold to what we should not paint a pixel on the screen (to cut polygons according to texture alpha)
    shader->setUniform("u_alpha_cutoff", material->alpha_mode == GTR::eAlphaMode::MASK ? material->alpha_cutoff : 0);
    shader->setUniform("u_ambient_light", object.current_scene->ambient_light);
    
    //use alpha once during blending
    shader->setUniform("u_use_alpha", true);
    
    //select the blending
    material->alpha_mode == GTR::eAlphaMode::BLEND ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
    
    material->alpha_mode == GTR::eAlphaMode::BLEND ?
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA) : glBlendFunc(GL_SRC_ALPHA, GL_ONE);
}

void Renderer::renderMultipass(Mesh *mesh, Shader *shader) {
    for(auto light : lights){
        
        shader->setUniform("u_light_type", (int)light->type);
        
        shader->setUniform("u_light_color", light->color * light->intensity);
        shader->setUniform("u_light_position", light->model.getTranslation());
        shader->setUniform("u_max_distance", light->max_dist);
        float light_angle_cosine = cos(light->cone_angle * DEG2RAD);
        shader->setUniform("u_cone_angle_cos", light_angle_cosine);
        shader->setUniform("u_cone_exp", light->cone_exp);
        shader->setUniform("u_light_direction", light->model.rotateVector(Vector3(0, 0, 1)).normalize());
        
        shader->setUniform("target", light->target);
        //do the draw call that renders the mesh into the screen
        mesh->render(GL_TRIANGLES);
        
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        
        shader->setUniform("u_ambient_light", Vector3());
        //tell shader to not add alpha
        shader->setUniform("u_use_alpha", false);
        //std::cout << i;
    }
}

//renders a mesh given its transform and material
void Renderer::renderMeshWithMaterial(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera)
{

	//in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material )
		return;
    assert(glGetError() == GL_NO_ERROR);

	//define locals to simplify coding
	Shader* shader = NULL;
	Texture* color_texture = NULL;
    Texture* emissive_texture = NULL;
    Texture* metallic_texture = NULL;
    Texture* normal_texture = NULL;
    Texture* occlusion_texture = NULL;

	color_texture = material->color_texture.texture;
	emissive_texture = material->emissive_texture.texture;
    metallic_texture = material->metallic_roughness_texture.texture;
	normal_texture = material->normal_texture.texture;
	occlusion_texture = material->occlusion_texture.texture;
	if (color_texture == NULL)
		color_texture = Texture::getWhiteTexture(); //a 1x1 white texture

	//select if render both sides of the triangles
	if(material->two_sided)
		glDisable(GL_CULL_FACE);
	else
		glEnable(GL_CULL_FACE);
    assert(glGetError() == GL_NO_ERROR);

	//chose a shader
    bool multipass = Application::instance->multipass_shader;
	shader = multipass ? Shader::Get("multiphong") : Shader::Get("singlephong");

    assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader)
		return;
	shader->enable();

	//upload uniforms
    uploadCommonData(*this, camera, material, model, shader, color_texture, emissive_texture, metallic_texture, normal_texture, occlusion_texture);

    
    glDepthFunc(GL_LEQUAL);
    
    if (multipass)
        renderMultipass(mesh, shader);
    else{
        Vector3 positions[MAX_LIGHTS];
        Vector3 light_color[MAX_LIGHTS];
        int type[MAX_LIGHTS];
        float max_distance[MAX_LIGHTS];
        float angle[MAX_LIGHTS];
        float cone_angle_cosine[MAX_LIGHTS];
        float cone_exp[MAX_LIGHTS];
        Vector3 light_direction[MAX_LIGHTS];
        Vector3 target[MAX_LIGHTS];
        //do the draw call that renders the mesh into the screen
        for (int i = 0; i < num_lights; i++){
            LightEntity* light = lights[i];
            positions[i] = light->model.getTranslation();
            light_color[i] = light->color;
            type[i] = (int)light->type;
            max_distance[i] = light->max_dist;
            angle[i] = light->angle;
            cone_angle_cosine[i] = cos(light->cone_angle * DEG2RAD);
            cone_exp[i] = light->cone_exp;
            light_direction[i] = light->model.rotateVector(Vector3(0, 0, 1)).normalize();
            target[i] = light->target;
        }
        
    }

	//disable shader

	shader->disable();

	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
    glDepthFunc(GL_LESS);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}


Texture* GTR::CubemapFromHDRE(const char* filename)
{
	HDRE* hdre = HDRE::Get(filename);
	if (!hdre)
		return NULL;

	Texture* texture = new Texture();
	if (hdre->getFacesf(0))
	{
		texture->createCubemap(hdre->width, hdre->height, (Uint8**)hdre->getFacesf(0),
			hdre->header.numChannels == 3 ? GL_RGB : GL_RGBA, GL_FLOAT);
		for (int i = 1; i < hdre->levels; ++i)
			texture->uploadCubemap(texture->format, texture->type, false,
				(Uint8**)hdre->getFacesf(i), GL_RGBA32F, i);
	}
	else
		if (hdre->getFacesh(0))
		{
			texture->createCubemap(hdre->width, hdre->height, (Uint8**)hdre->getFacesh(0),
				hdre->header.numChannels == 3 ? GL_RGB : GL_RGBA, GL_HALF_FLOAT);
			for (int i = 1; i < hdre->levels; ++i)
				texture->uploadCubemap(texture->format, texture->type, false,
					(Uint8**)hdre->getFacesh(i), GL_RGBA16F, i);
		}
	return texture;
}
