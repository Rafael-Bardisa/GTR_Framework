#include "renderer.h"
#include "fbo.h"
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

#warning uso warning para resaltar comentarios en xcode que si no no se ven

#warning DOUBT dynamic indexes?
Vector4 atlas_indexes[MAX_LIGHTS] = {
    Vector4(0, 0, 0.5, 0.5),
    Vector4(0.5, 0, 0.5, 0.5),
    Vector4(0, 0.5, 0.5, 0.5),
    Vector4(0.5, 0.5, 0.25, 0.25),
    Vector4(0.75, 0.5, 0.25, 0.25),
    Vector4(0.5, 0.75, 0.25, 0.25),
};


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
    pipeline = FORWARD;
    gbuffers = nullptr;
    illumination_fbo = nullptr;
    
    //shadow texture initialization
    shadow_atlas = new Texture();
    shadow_fbo = new FBO();
    shadow_fbo->setDepthOnly(2048, 2048);
    shadow_atlas = shadow_fbo->depth_texture;
    show_shadow_atlas = false;
    
}

void Renderer::showShadowAtlas() {
    // split into parts because it is atlas -> Each camera will have different near far
    Shader* shader = Shader::getDefaultShader("depth");
    shader->enable();
    //since it is shadow atlas, each light goes to its own slot in viewport
    //for (auto light:lights){
    //    shader->setUniform("u_camera_nearfar", Vector2(light->light_camera->near_plane, light->light_camera->far_plane));
    //    shader->setUniform("u_light_shadow_slot", light->atlas_shadowmap_dimensions);
        
    shader->setUniform("u_camera_nearfar", Vector2(0.1, 150));
    shadow_atlas->toViewport(shader);
    shader->disable();
}

void Renderer::renderForward(Camera *camera) {
#warning TODO hacerlo consistente con otras funciones
    for(auto instruction = instructions.begin(); instruction != instructions.end(); instruction++) {
        if (camera->testBoxInFrustum(instruction->bounding_box.center, instruction->bounding_box.halfsize))
            renderInstruction(*instruction, camera);
    }
}



void Renderer::show_gbuffers(Camera *camera, int w, int h) {
    //remember to disable ztest when rendering quads!
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    
    //set an area of the screen and render fullscreen quad
    glViewport(0, h*0.5, w * 0.5, h * 0.5);
    gbuffers->color_textures[0]->toViewport(); //colorbuffer
    
    glViewport(w*0.5, h*0.5, w * 0.5, h * 0.5);
    gbuffers->color_textures[1]->toViewport(); //normalbuffer
    
    glViewport(w*0.5, 0, w * 0.5, h * 0.5);
    gbuffers->color_textures[2]->toViewport(); //normalbuffer
    
    //for the depth remember to linearize when displaying it
    glViewport(0, 0, w * 0.5, h * 0.5);
    Shader* depth_shader = Shader::getDefaultShader("linear_depth");
    depth_shader->enable();
    Vector2 near_far = Vector2(camera->near_plane, camera->far_plane);
    depth_shader->setUniform("u_camera_nearfar", near_far);
    gbuffers->depth_texture->toViewport(depth_shader);
    
    //set the viewport back to full screen
    glViewport(0,0,w,h);
}

void Renderer::renderDeferred(Camera* camera){
    //Gbuffers
    Application* instance = Application::instance;
    int w = instance->window_width;
    int h = instance->window_height;
    if (!gbuffers){
    gbuffers = new FBO();
#warning TODO aligual como mucho 4 buffers o el maximo de datos para no pillar el render
    gbuffers->create(w, h, 3, GL_RGBA, GL_UNSIGNED_BYTE, true);
    }
    
    if (!illumination_fbo){
    illumination_fbo = new FBO();

    illumination_fbo->create(w, h, 1, GL_RGB, GL_UNSIGNED_BYTE, true);
    }
    //start rendering inside the gbuffers
    gbuffers->bind();

    //we clear in several passes so we can control the clear color independently for every gbuffer

    //disable all but the GB0 (and the depth)
    gbuffers->enableSingleBuffer(0);

    //clear GB0 with the color (and depth)
    glClearColor(0.1,0.1,0.1,1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    //and now enable the second GB to clear it to black
    gbuffers->enableSingleBuffer(1);
    glClearColor(0.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);
    
    gbuffers->enableSingleBuffer(2);
    glClearColor(0.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);


    //enable all buffers back
    gbuffers->enableAllBuffers();

    Shader* shader = Shader::Get("gbuffers");
    shader->enable();
    //render everything
    //...
    for (auto instruction : instructions){
        uploadCommonData(camera, instruction.material, instruction.model, shader);
        
        shader->setUniform("u_dither", instruction.material->alpha_mode == eAlphaMode::BLEND);
        
        instruction.mesh->render(GL_TRIANGLES);
    }

    //stop rendering to the gbuffers
    shader->disable();
    gbuffers->unbind();
    
    show_gbuffers(camera, w, h);

    //render cada obj con un shader gbuffer
    
    //start rendering to the illumination fbo
    illumination_fbo->bind();

    //we need a fullscreen quad
    Mesh* quad = Mesh::getQuad();

    //we need a shader specially for this task, lets call it "deferred"
    shader = Shader::Get("deferred");
    shader->enable();

    //pass the gbuffers to the shader
    shader->setUniform("u_color_texture", gbuffers->color_textures[0], 0);
    shader->setUniform("u_normal_texture", gbuffers->color_textures[1], 1);
    shader->setUniform("u_extra_texture", gbuffers->color_textures[2], 2);
    shader->setUniform("u_depth_texture", gbuffers->depth_texture, 3);

    //pass the inverse projection of the camera to reconstruct world pos.
    Matrix44 inv_vp = camera->viewprojection_matrix;
    inv_vp.inverse();
    
    shader->setUniform("u_inverse_viewprojection", inv_vp);
    //pass the inverse window resolution, this may be useful
    shader->setUniform("u_iRes", Vector2(1.0 / (float)w, 1.0 / (float)h));

    //pass all the information about the light and ambient…
    //...
    //disable depth test and blend!!
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    //render a fullscreen quad
    quad->render(GL_TRIANGLES);

    illumination_fbo->unbind();
     
    illumination_fbo->color_textures[0]->toViewport();
     
    //renderizar a pantalla leyendo de gbuffer
}

//loads the entities from the scene and stores them conveniently in the class. Uses camera to avoid rendering out of bounds entities
void Renderer::loadScene(Camera *camera, GTR::Scene *&scene) {
    instructions.clear();
    lights.clear();
    
    // Clear the color and the depth buffer
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    checkGLErrors();
    
    
    for (int i = 0; i < scene->entities.size(); ++i)
#warning TODO get lights-> test lights against camera-> get prefabs-> test prefabs against all cameras-> optimized scene
    {
        BaseEntity* ent = scene->entities[i];
        if (!ent->visible)
            continue;
        
        if (ent->entity_type == LIGHT){
            auto light_ent = (GTR::LightEntity*)ent;
            //break out of switch if light outside frustrum, else continue to next entity
            switch(light_ent->type){
                case UNKNOWN:
                    continue;
                case POINT:
                    if (camera->testSphereInFrustum(light_ent->model * Vector3(), light_ent->max_dist))
                        break;
                    continue;
                case SPOT:
                    break;
                    continue;
                case DIRECTIONAL:
                    break;
                default:
                    continue;
            }
            lights.push_back(light_ent);
        }
    }
    num_lights = (int)lights.size();
    
    for (int i = 0; i < scene->entities.size(); ++i)
#warning TODO get lights-> test lights against camera-> get prefabs-> test prefabs against all cameras-> optimized scene
    {
        BaseEntity* ent = scene->entities[i];
        if (!ent->visible)
            continue;
        
        //is a prefab!
        if (ent->entity_type == PREFAB)
        {
            PrefabEntity* pent = (GTR::PrefabEntity*)ent;
            if(pent->prefab)
#warning TODO queuePrefab should check if prefab receives light from any source or if inside camera frustum
                queuePrefab(ent->model, pent->prefab, camera);
        }
    }
        
    // sort node vector by priority
    std::sort(instructions.begin(), instructions.end(), GTR::renderPriority);
    
    generateShadowAtlas();
}

void Renderer::renderScene(GTR::Scene* scene, Camera* camera)
{
    current_scene = scene;
	//set the clear color (the background color)
	glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);
    
    //empty instructions and lights list
    loadScene(camera, scene);

    // render nodes by priority
    
    //each node rendered with all the lights
#warning TODO here choose pipeline
    pipeline == FORWARD ?
        renderForward(camera):
        renderDeferred(camera);
    //showShadowmap(lights[0]);
    if (show_shadow_atlas)
    showShadowAtlas();
}

// compare distance to current camera
bool light_distance(const LightEntity* first, const LightEntity* second){
    float first_distance = first->type == DIRECTIONAL ? 0.0 : (first->model * Vector3()).distance(Camera::current->eye);
    float second_distance = second->type == DIRECTIONAL ? 0.0 : (second->model * Vector3()).distance(Camera::current->eye);
    
    return first_distance < second_distance;
}

//change viewport from texture size and normalized size
void glViewport(Vector2 texture_dimensions, Vector4 size){
    glViewport(texture_dimensions.x * size.x,
               texture_dimensions.y * size.y,
               texture_dimensions.x * size.z,
               texture_dimensions.y * size.w);
}

// for each light, assign a slot in shadow atlas. Directional and closer get more resolution
void Renderer::generateShadowAtlas(){
    
    Application* instance = Application::instance;
    
    int w = instance->window_width;
    int h = instance->window_height;
    
    int shadow_idx = 0;
    //give closer and directional lights preference in the shadow atlas
    std::sort(lights.begin(), lights.end(), light_distance);
    
    Camera* view_camera = Camera::current;
    
    shadow_fbo->bind();
    
    int shadow_w = shadow_atlas->width;
    int shadow_h = shadow_atlas->height;
    
    Vector2 shadow_atlas_dimensions = Vector2(shadow_w, shadow_h);

    for (auto* light:lights){
        if (!light->cast_shadows) continue;
        

        //assign slot coordinates to light
#warning TODO atlas indexes dynamic? how
        light->atlas_shadowmap_dimensions = atlas_indexes[shadow_idx];
        
        shadow_idx++;
        //change viewport here to not modify render flat mesh
        glViewport(shadow_atlas_dimensions, light->atlas_shadowmap_dimensions);
        
        //generate shadowmap of light
        generateShadowMap(light, view_camera);
        
    }
    shadow_fbo->unbind();
    view_camera->enable();
    //reset viewport
    glViewport(Vector2(w, h), Vector4(0,0,1,1));
    
}



// given a light, uses its dimensions to use a part of the atlas for itself
// since this is used in generate shadow atlas, which is a loop through all lights, we can skip resetting the viewport
// and only resetting in atlas function
void Renderer::generateShadowMap(LightEntity* light, Camera* view_camera){
    if (!light->cast_shadows) return;
    //light->fbo = new FBO();
    //light->fbo->setDepthOnly(1024, 1024);
    //light->shadow_map = light->fbo->depth_texture;

    if(!light->light_camera) light->light_camera = new Camera();
    
    //change viewport according to light shadow dimensions
    light->configCamera();
    Camera* light_camera = light->light_camera;
    light_camera->enable();
    
#warning DOUBT directional lights stay in the camera position lol esto esta un poco mal
    // works with the spot but not with directional???
    if (light->type == SPOT){
        light->area_size = 1500;
        Camera* light_camera = light->light_camera;
        light->configCamera();
        light_camera->moveGlobal(view_camera->eye - light_camera->eye);
    }
    
    glClear(GL_DEPTH_BUFFER_BIT);
    for (auto instruction : instructions){
        //skip transparent entities
        if (instruction.material->alpha_mode == eAlphaMode::BLEND) continue;
        
        renderFlatMesh(instruction.model, instruction.mesh, instruction.material, light_camera);
    }
}

//adds nodes of prefab to renderer node list
void Renderer::queuePrefab(const Matrix44& model, GTR::Prefab* prefab, Camera* camera)
{
	assert(prefab && "PREFAB IS NULL");
	//assign the model to the root node
	queueNode(model, &prefab->root, camera);
}

//adds a node, if visible, to the node vector and recursively calls on its children
void Renderer::queueNode(const Matrix44& prefab_model, GTR::Node* node, Camera* camera)
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
		
#warning TODO if bounding box is inside any camera frustum then the object is probably visible
        //if (std::any_of(lights.begin(), lights.end(), [&world_bounding](LightEntity* light) {light->light_camera->testBoxInFrustum(world_bounding.center, world_bounding.halfsize);})){

            instructions.push_back(RenderInstruct( node_model, node->mesh, node->material, camera->eye.distance(world_bounding.center), world_bounding));
            
        }
			//node->mesh->renderBounding(node_model, true);
            
		//}
	

	//iterate recursively with children
	for (int i = 0; i < node->children.size(); ++i)
		queueNode(prefab_model, node->children[i], camera);
}

//upload material and camera info which is common in multipass
void Renderer::uploadCommonData(Camera *camera, GTR::Material *material, const Matrix44 &model, Shader *shader) {
    shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
    shader->setUniform("u_camera_position", camera->eye);
    shader->setUniform("u_model", model );
    float t = getTime();
    shader->setUniform("u_time", t );
    
    Texture* color_texture = material->color_texture.texture;
    Texture* emissive_texture = material->emissive_texture.texture;
    Texture* normal_texture = material->normal_texture.texture;
    Texture* metallic_texture = material->metallic_roughness_texture.texture;
    Texture* occlusion_texture = material->occlusion_texture.texture;
    
    shader->setUniform("u_color", material->color);
    Texture* to_send = color_texture ? color_texture : Texture::getWhiteTexture();
    shader->setUniform("u_color_texture", to_send, 0);
    
    to_send = emissive_texture ? emissive_texture : Texture::getBlackTexture();
    shader->setUniform("u_emissive_texture", to_send, 1);
    
    to_send = metallic_texture ? metallic_texture : Texture::getBlackTexture();
    shader->setUniform("u_metallic_texture", to_send, 2);
    
    //if para guardar recursos
    shader->setUniform("u_use_normalmap", normal_texture ? true : false);
    if (normal_texture){
        to_send = normal_texture;
        shader->setUniform("u_normal_texture", to_send, 3);
    }
    
    to_send = occlusion_texture ? occlusion_texture : Texture::getWhiteTexture();
    shader->setUniform("u_occlusion_texture", to_send, 4);
    
    //assuming a maximum of 8 textures for gpu
    to_send = shadow_atlas ? shadow_atlas : Texture::getWhiteTexture();
    shader->setUniform("u_shadow_atlas", to_send, 7);

    
    
    //this is used to say which is the alpha threshold to what we should not paint a pixel on the screen (to cut polygons according to texture alpha)
    shader->setUniform("u_alpha_cutoff", material->alpha_mode == GTR::eAlphaMode::MASK ? material->alpha_cutoff : 0);
    shader->setUniform("u_ambient_light", current_scene->ambient_light);
    //use alpha once during blending
    //shader->setUniform("u_use_alpha", true);
}

void Renderer::uploadLightData(LightEntity* light, Shader* shader){
    shader->setUniform("u_light_type", (int)light->type);
    
    shader->setUniform("u_light_color", light->color * light->intensity);
    shader->setUniform("u_light_position", light->model.getTranslation());
    shader->setUniform("u_max_distance", light->max_dist);
    float light_angle_cosine = cos(light->cone_angle * DEG2RAD);
    //shader->setUniform("u_angle", light->angle);
    shader->setUniform("u_cone_angle_cos", light_angle_cosine);
    shader->setUniform("u_cone_exp", light->cone_exp);
    shader->setUniform("u_light_direction", light->model.rotateVector(Vector3(0, 0, -1)).normalize());
    
    shader->setUniform("u_target", light->target);
    
#warning TODO reactivar sombras
    Matrix44 default_viewprojection;
    default_viewprojection.setIdentity();
    
    bool use_shadows = /*light->cast_shadows*/ false;

    shader->setUniform("u_cast_shadows", use_shadows );
    shader->setUniform("u_shadow_bias", use_shadows ? light->shadow_bias : 0.f);
    shader->setUniform("u_shadowmap_dimensions", light->atlas_shadowmap_dimensions);
    shader->setUniform("u_light_viewprojection", use_shadows ? light->light_camera->viewprojection_matrix : default_viewprojection);
        
    
}


void Renderer::renderMultipass(Mesh *mesh, Shader *shader) {
    //tell shader it is the first pass so it should use the alpha
    shader->setUniform("u_first_pass", true);
    for(auto light : lights){
        uploadLightData(light, shader);
        //do the draw call that renders the mesh into the screen
        mesh->render(GL_TRIANGLES);
        
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        //glBlendFunc( GL_SRC_ALPHA,GL_ONE );
        
        //tell shader to not add alpha, ambient and emissive
        shader->setUniform("u_ambient_light", Vector3());
        shader->setUniform("u_first_pass", false);
    }
}

//upload info of all lights to shader
void Renderer::uploadLightsData(Shader *shader) {
    //light data
    Vector3 positions[MAX_LIGHTS];
    Vector3 light_color[MAX_LIGHTS];
    int type[MAX_LIGHTS];
    float max_distance[MAX_LIGHTS];
    //float angle[MAX_LIGHTS];
    float cone_angle_cosine[MAX_LIGHTS];
    float cone_exp[MAX_LIGHTS];
    Vector3 light_direction[MAX_LIGHTS];
    Vector3 target[MAX_LIGHTS];
    
    //shadow data
    bool cast_shadows[MAX_LIGHTS];
    float shadow_bias[MAX_LIGHTS];
    Vector4 shadow_dimensions[MAX_LIGHTS];
    Matrix44 light_viewprojection[MAX_LIGHTS];
    
    //default viewproj to fill array
    Matrix44 default_viewprojection;
    default_viewprojection.setIdentity();
    
    //fill the arrays of light information to send to the shader
    for (int i = 0; i < num_lights; i++){
        LightEntity* light = lights[i];
        
        //light data
        positions[i] = light->model.getTranslation();
        light_color[i] = light->color * light->intensity;
        type[i] = (int)light->type;
        max_distance[i] = light->max_dist;
        //angle[i] = light->angle;
        cone_angle_cosine[i] = cos(light->cone_angle * DEG2RAD);
        cone_exp[i] = light->cone_exp;
        light_direction[i] = light->model.rotateVector(Vector3(0, 0, -1)).normalize();
        target[i] = light->target;
        
#warning TODO activar sombras
        //shadow data
        bool use_shadows = /*light->cast_shadows*/ false;
        
        cast_shadows[i] = use_shadows;
        shadow_bias[i] = use_shadows ? light->shadow_bias : 0.f;
        shadow_dimensions[i] = light->atlas_shadowmap_dimensions;
        light_viewprojection[i] = use_shadows ? light->light_camera->viewprojection_matrix : default_viewprojection;
    }
    
    //upload information to the shader
    shader->setUniform("u_num_lights", num_lights);
    
    //light data
    shader->setUniform3Array("u_light_position", (float*)&positions, MAX_LIGHTS);
    shader->setUniform3Array("u_light_color", (float*)&light_color, MAX_LIGHTS);
    shader->setUniform1Array("u_light_type", (int*)&type, MAX_LIGHTS);
    shader->setUniform1Array("u_max_distance", (float*)&max_distance, MAX_LIGHTS);
    //shader->setUniform3Array("u_angle", (float*)&angle, MAX_LIGHTS);
    shader->setUniform1Array("u_cone_angle_cosine", (float*)&cone_angle_cosine, MAX_LIGHTS);
    shader->setUniform1Array("u_cone_exp", (float*)&cone_exp, MAX_LIGHTS);
    shader->setUniform3Array("u_light_direction", (float*)&light_direction, MAX_LIGHTS);
    shader->setUniform3Array("u_target", (float*)&target, MAX_LIGHTS);
    
    //shadow data
    shader->setUniform1Array("u_cast_shadows", (int*)&cast_shadows, MAX_LIGHTS);
    shader->setUniform1Array("u_shadow_bias", (float*)&shadow_bias, MAX_LIGHTS);
    shader->setUniform4Array("u_shadowmap_dimensions", (float*)&shadow_dimensions, MAX_LIGHTS);
#warning DOUBT singlepass y multipass no son equivalentes y activar la linea de abajo quita la spot en singlepass
    //shader->setMatrix44Array("u_light_viewprojection", (Matrix44*)&light_viewprojection, MAX_LIGHTS);
}

//upload info of all lights and render mesh with one call
void Renderer::renderSinglepass(Mesh *mesh, Shader *shader) {
    uploadLightsData(shader);
    
    mesh->render(GL_TRIANGLES);
    
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

	//select if render both sides of the triangles
	if(material->two_sided)
		glDisable(GL_CULL_FACE);
	else
		glEnable(GL_CULL_FACE);
    assert(glGetError() == GL_NO_ERROR);

	//chose a shader
    bool multipass = shaderpass == MULTIPASS;
    
    shader = multipass ? Shader::Get("multiphong") : Shader::Get("singlephong");

    assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader)
		return;
	shader->enable();

	//upload uniforms
    
    //refactor to get rid of redundant attributes
    uploadCommonData(camera, material, model, shader);

    //select the blending
    material->alpha_mode == GTR::eAlphaMode::BLEND ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
    material->alpha_mode == GTR::eAlphaMode::BLEND ?
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA) : glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    
    if (multipass){
        glDepthFunc(GL_LEQUAL);
        renderMultipass(mesh, shader);
    }
    else{
        renderSinglepass(mesh, shader);
    }
    
	//disable shader

	shader->disable();

	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
    glDepthFunc(GL_LESS);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Renderer::renderFlatMesh(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera)
{

    //in case there is nothing to do
    if (!mesh || !mesh->getNumVertices() || !material )
        return;
    assert(glGetError() == GL_NO_ERROR);

    //define locals to simplify coding
    Shader* shader = NULL;
    
    //select if render both sides of the triangles
    if(material->two_sided)
        glDisable(GL_CULL_FACE);
    else
        glEnable(GL_CULL_FACE);
    assert(glGetError() == GL_NO_ERROR);

    //chose a shader
    shader = Shader::Get("flat");

    assert(glGetError() == GL_NO_ERROR);

    //no shader? then nothing to render
    if (!shader)
        return;
    shader->enable();

    //upload uniforms
    shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
    shader->setUniform("u_model", model);
    shader->setUniform("u_alpha_cutoff", material->alpha_mode == GTR::eAlphaMode::MASK ? material->alpha_cutoff : 0);
    
    glDisable(GL_BLEND);
    glDepthFunc(GL_LESS);
    
    //disable shader
    mesh->render(GL_TRIANGLES);

    shader->disable();
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
