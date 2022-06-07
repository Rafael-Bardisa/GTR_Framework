#pragma once
#include "prefab.h"
#include "stdlib.h"
#include <algorithm>
#define MAX_LIGHTS 6
//forward declarations
class Camera;
class Shader;
namespace GTR {

    

	class Prefab;
	class Material;

    class RenderInstruct {
        
    public:
        Matrix44 model;
        Mesh* mesh;
        GTR::Material* material;
        float distance;
        BoundingBox bounding_box;
        
        RenderInstruct(Matrix44 model, Mesh* mesh, GTR::Material* material, float distance, BoundingBox bounding_box){
            this->model = model;
            this->mesh = mesh;
            this->material = material;
            this->distance = distance;
            this->bounding_box = bounding_box;
        }
    };
	
	// This class is in charge of rendering anything in our system.
	// Separating the render from anything else makes the code cleaner
	class Renderer
	{
    public:
        enum ePipeline{
            FORWARD,
            DEFERRED
        };
        enum eShaderPass{
            SINGLEPASS,
            MULTIPASS
        };
        
        FBO* gbuffers;
        FBO* illumination_fbo;
        
        ePipeline pipeline;
        eShaderPass shaderpass;
        
        std::vector<RenderInstruct> instructions;
        std::vector<GTR::LightEntity*> lights;
        
        int num_lights;
        Scene* current_scene;
        
        Texture* shadow_atlas;
        FBO* shadow_fbo;
        bool show_shadow_atlas;
		//add here your functions
		//...
        
        Renderer();
        
        void loadScene(Camera *camera, GTR::Scene *&scene);
        
        void renderForward(Camera *camera);
        void show_gbuffers(Camera *camera, int h, int w);
        
        void renderDeferred(Camera* camera);
        
        void extracted(Camera *camera, GTR::Scene *&scene);
        
//renders several elements of the scene
		void renderScene(GTR::Scene* scene, Camera* camera);
	
		//to render a whole prefab (with all its nodes)
		void queuePrefab(const Matrix44& model, GTR::Prefab* prefab, Camera* camera);

		//to render one node from the prefab and its children
		void queueNode(const Matrix44& model, GTR::Node* node, Camera* camera);

        void renderMultipass(Mesh *mesh, Shader *shader);

        [[deprecated("I'll use multipass")]]
        void extracted(Shader *shader);
        
        void renderSinglepass(Mesh *mesh, Shader *shader);
        
        //uploads to shader
        //object properties
        void uploadCommonData(Camera *camera, GTR::Material *material, const Matrix44 &model, Shader *shader);
        
        //light properties
        void uploadLightData(LightEntity* light, Shader* shader);
        void uploadLightsData(Shader *shader);
        
//to render one mesh given its material and transformation matrix
		void renderMeshWithMaterial(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera);
        //to render one mesh with render instruction
        void renderFlatMesh(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera);
        inline void renderInstruction(const RenderInstruct instruction, Camera* camera){
            renderMeshWithMaterial(instruction.model,
                                   instruction.mesh,
                                   instruction.material,
                                   camera);};
        
        Camera *extracted(GTR::LightEntity *light);
        
        void generateShadowAtlas();
        void generateShadowMap(LightEntity* light, Camera* view_camera);
        
        void showShadowAtlas();
        };

	Texture* CubemapFromHDRE(const char* filename);
    
bool renderPriority(const RenderInstruct& first, const RenderInstruct& second);

};
