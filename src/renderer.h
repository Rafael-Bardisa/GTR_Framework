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
        
        RenderInstruct(Matrix44 model, Mesh* mesh, GTR::Material* material, float distance){
            this->model = model;
            this->mesh = mesh;
            this->material = material;
            this->distance = distance;
        }
    };
	
	// This class is in charge of rendering anything in our system.
	// Separating the render from anything else makes the code cleaner
	class Renderer
	{
        
	public:
        std::vector<RenderInstruct> instructions;
        std::vector<GTR::LightEntity*> lights;
        int num_lights;
        Scene* current_scene;
		//add here your functions
		//...
        
        Renderer();

        void extracted();
        
//renders several elements of the scene
		void renderScene(GTR::Scene* scene, Camera* camera);
	
		//to render a whole prefab (with all its nodes)
		void renderPrefab(const Matrix44& model, GTR::Prefab* prefab, Camera* camera);

		//to render one node from the prefab and its children
		void renderNode(const Matrix44& model, GTR::Node* node, Camera* camera);

        void renderMultipass(Mesh *mesh, Shader *shader);
        void renderSinglepass(Mesh *mesh, Shader *shader);
        
        void extracted(Mesh *mesh, bool multipass, Shader *shader);
        
        void extracted(Texture *&color_texture, Texture *&emissive_texture, GTR::Material *material, Mesh *mesh, Texture *&metallic_texture, bool &multipass, Texture *&normal_texture, Texture *&occlusion_texture, Shader *&shader);
        
        void extracted(GTR::Material *material, Mesh *mesh, bool multipass, Shader *shader);
        
        void extracted(Shader *shader);
        
//to render one mesh given its material and transformation matrix
		void renderMeshWithMaterial(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera);
        //to render one mesh with render instruction
        void renderFlatMesh(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera);
        inline void renderInstruction(const RenderInstruct instruction, Camera* camera){
            renderMeshWithMaterial(instruction.model,
                                   instruction.mesh,
                                   instruction.material,
                                   camera);};
        
        void generateShadowMap(LightEntity* light);
        void showShadowmap(LightEntity* light);
	};

	Texture* CubemapFromHDRE(const char* filename);
    
bool renderPriority(const RenderInstruct& first, const RenderInstruct& second);

};
