#include "lgfx-astral/Shader.hpp"
#include <math.h>
#include <stdio.h>

using namespace Json;

namespace LGFX
{
    Shader::Shader()
    {
        this->allocator = IAllocator{};
        gpuFunction = NULL;
        uniforms = ShaderVariables();

        this->descriptorForThisDrawCall = 0;
        this->descriptorSets = collections::vector<void *>();
    }
    Shader::Shader(IAllocator allocator)
    {
        this->allocator = allocator;
        gpuFunction = NULL;
        uniforms = ShaderVariables(allocator, ShaderResource{});

        this->descriptorForThisDrawCall = 0;
        this->descriptorSets = collections::vector<void *>(allocator);
    }
    void ShaderResource::deinit()
    {
        nameStr.deinit();
        LGFXShaderResourceType resourceType = resource.type;
        for (u32 j = 0; j < states.count; j++)
        {
            LGFXDestroyFunctionVariable(states.ptr[j]);
        }
        states.deinit();
    }
    void Shader::deinit()
    {
        LGFXDestroyFunction(this->gpuFunction);
        for (u32 i = 0; i < uniforms.capacity; i++)
        {
            if (uniforms.ptr[i].nameStr.buffer == NULL)
            {
                continue;
            }
            uniforms.ptr[i].deinit();
        }
        uniforms.deinit();
    }

    void Shader::SetShaderVariableComputeBuffer(const char* variableName, LGFXBuffer buffer)
    {
        CheckDescriptorSetAvailability();
        for (usize i = 0; i < uniforms.capacity; i++)
        {
            if (uniforms.ptr[i].nameStr.buffer == NULL)
            {
                break;
            }
            
            if (uniforms.ptr[i].nameStr == variableName)
            {
                //uniformsHasBeenSet = true;
                uniforms.ptr[i].states.ptr[descriptorForThisDrawCall].mutated = true;
                ((LGFXBuffer *)uniforms.ptr[i].states.ptr[descriptorForThisDrawCall].currentValues)[0] = buffer;
                break;
            }
        }
    }
    void Shader::SetShaderVariable(const char* variableName, void* ptr, usize size)
    {
        CheckDescriptorSetAvailability();
        for (usize i = 0; i < uniforms.capacity; i++)
        {
            if (uniforms.ptr[i].nameStr.buffer == NULL)
            {
                break;
            }
            
            if (uniforms.ptr[i].nameStr == variableName)
            {
                uniforms.ptr[i].states.ptr[descriptorForThisDrawCall].mutated = true;
                LGFXSetBufferDataFast(((LGFXBuffer *)uniforms.ptr[i].states.ptr[descriptorForThisDrawCall].currentValues)[0], (u8*)ptr, size);
                break;
                //uniforms.ptr[i].states.ptr[descriptorForThisDrawCall].ub.SetData(ptr, size);
            }
        }
    }
    void Shader::SetShaderVariableTextures(const char* variableName, LGFXTexture*textures, usize count)
    {
        CheckDescriptorSetAvailability();
        for (usize i = 0; i < uniforms.capacity; i++)
        {
            if (uniforms.ptr[i].nameStr.buffer == NULL)
            {
                break;
            }
            if (uniforms.ptr[i].nameStr == variableName)
            {
                LGFXFunctionVariable *mutableState = &uniforms.ptr[i].states.ptr[descriptorForThisDrawCall];
                mutableState->mutated = true;
                for (usize j = 0; j < count; j++)
                {
                   ((LGFXTexture *)mutableState->currentValues)[j] = textures[j];
                }
                break;
            }
        }
    }
    void Shader::SetShaderVariableTexture(const char* variableName, LGFXTexture texture)
    {
        this->SetShaderVariableTextures(variableName, &texture, 1);
    }
    void Shader::SetShaderVariableSamplers(const char* variableName, LGFXSamplerState *samplers, usize count)
    {
        CheckDescriptorSetAvailability();
        for (usize i = 0; i < uniforms.capacity; i++)
        {
            if (uniforms.ptr[i].nameStr.buffer == NULL)
            {
                break;
            }
            if (uniforms.ptr[i].nameStr == variableName)
            {
                LGFXFunctionVariable *mutableState = &uniforms.ptr[i].states.ptr[descriptorForThisDrawCall];
                mutableState->mutated = true;
                for (usize j = 0; j < count; j++)
                {
                   ((LGFXSamplerState *)mutableState->currentValues)[j] = samplers[j];
                }
                break;
            }
        }
    }
    void Shader::SetShaderVariableSampler(const char* variableName, LGFXSamplerState sampler)
    {
        this->SetShaderVariableSamplers(variableName, &sampler, 1);
    }

    void Shader::CheckDescriptorSetAvailability(bool forceAddNewDescriptor)
    {
        for (u32 i = 0; i < this->uniforms.capacity; i++)
        {
            if (uniforms.ptr[i].nameStr.buffer == NULL)
            {
                break;
            }

            if (forceAddNewDescriptor || this->descriptorForThisDrawCall >= this->uniforms.ptr[i].states.count)
            {
                LGFXFunctionVariable newVarSlot = LGFXFunctionGetVariableSlot(this->gpuFunction, i);
                this->uniforms.ptr[i].states.Add(newVarSlot);
            }
        }
    }
    void Shader::SyncUniformsWithGPU(void *commandEncoder)
    {
        LGFXFunctionVariable variables[32];
        u32 variablesCount = 0;
        for (usize i = 0; i < this->uniforms.capacity; i++)
        {
            if (this->uniforms.ptr[i].nameStr.buffer == NULL)
            {
                break;
            }
            variables[i] = this->uniforms.ptr[i].states.ptr[this->descriptorForThisDrawCall];
            variablesCount++;
        }
        LGFXFunctionSendVariablesToGPU(this->device, variables, variablesCount);
    }

    u32 ParseShaderVariables(Json::JsonElement *json, ShaderVariables *results, LGFXShaderInputAccessFlags accessedByShaderOfType)
    {
        u32 length = -1;
        JsonElement *uniforms = json->GetProperty("uniforms");
        if (uniforms != NULL)
        {
            for (usize i = 0; i < uniforms->arrayElements.length; i++)
            {
                string name = uniforms->arrayElements.data[i].GetProperty("name")->GetString(results->allocator);
                u32 stride = uniforms->arrayElements.data[i].GetProperty("stride")->GetUint32();
                u32 set = uniforms->arrayElements.data[i].GetProperty("set")->GetUint32();
                u32 binding = uniforms->arrayElements.data[i].GetProperty("binding")->GetUint32();

                if (binding > length)
                {
                    length = binding;
                }

                LGFX::ShaderResource *resource = results->Get(binding);
                if (resource != NULL && resource->resource.variableName != NULL)
                {
                    resource->resource.accessedBy = (LGFXShaderInputAccessFlags)((u32)resource->resource.accessedBy | accessedByShaderOfType);
                    name.deinit();
                }
                else
                {
                    ShaderResource newResource;
                    newResource.resource.binding = binding;
                    newResource.resource.set = set;
                    newResource.resource.variableName = name.buffer;
                    newResource.resource.size = stride;
                    newResource.resource.accessedBy = accessedByShaderOfType;
                    newResource.resource.arrayLength = 0;
                    newResource.resource.type = LGFXShaderResourceType_Uniform;

                    newResource.nameStr = name;
                    newResource.states = collections::vector<LGFXFunctionVariable>(results->allocator);
                    
                    results->Insert((usize)binding, newResource);
                }
                //results->uniforms.Add(binding, {name, set, binding, stride});
            }
        }
        JsonElement *textures = json->GetProperty("images");
        if (textures != NULL)
        {
            for (usize i = 0; i < textures->arrayElements.length; i++)
            {
                string name = textures->arrayElements.data[i].GetProperty("name")->GetString(results->allocator);
                u32 arrayLength = textures->arrayElements.data[i].GetProperty("arrayLength")->GetUint32();
                u32 set = textures->arrayElements.data[i].GetProperty("set")->GetUint32();
                u32 binding = textures->arrayElements.data[i].GetProperty("binding")->GetUint32();
                
                if (binding > length)
                {
                    length = binding;
                }

                LGFX::ShaderResource *resource = results->Get(binding);
                if (resource != NULL && resource->resource.variableName != NULL)
                {
                    resource->resource.accessedBy = (LGFXShaderInputAccessFlags)((u32)resource->resource.accessedBy | accessedByShaderOfType);
                    name.deinit();
                }
                else
                {
                    ShaderResource newResource;
                    newResource.resource.binding = binding;
                    newResource.resource.set = set;
                    newResource.resource.variableName = name.buffer;
                    newResource.resource.arrayLength = arrayLength;
                    newResource.resource.accessedBy = accessedByShaderOfType;
                    newResource.resource.type = LGFXShaderResourceType_Texture;
                    newResource.resource.size = 0;

                    newResource.nameStr = name;
                    newResource.states = collections::vector<LGFXFunctionVariable>(results->allocator);
                    
                    results->Insert((usize)binding, newResource);
                }
            }
        }
        JsonElement *samplers = json->GetProperty("samplers");
        if (samplers != NULL)
        {
            for (usize i = 0; i < samplers->arrayElements.length; i++)
            {
                string name = samplers->arrayElements.data[i].GetProperty("name")->GetString(results->allocator);
                u32 arrayLength = samplers->arrayElements.data[i].GetProperty("arrayLength")->GetUint32();
                u32 set = samplers->arrayElements.data[i].GetProperty("set")->GetUint32();
                u32 binding = samplers->arrayElements.data[i].GetProperty("binding")->GetUint32();

                if (binding > length)
                {
                    length = binding;
                }

                LGFX::ShaderResource *resource = results->Get(binding);
                if (resource != NULL && resource->resource.variableName != NULL)
                {
                    resource->resource.accessedBy = (LGFXShaderInputAccessFlags)((u32)resource->resource.accessedBy | accessedByShaderOfType);
                    name.deinit();
                }
                else
                {
                    ShaderResource newResource;
                    newResource.resource.binding = binding;
                    newResource.resource.set = set;
                    newResource.resource.variableName = name.buffer;
                    newResource.resource.arrayLength = arrayLength;
                    newResource.resource.accessedBy = accessedByShaderOfType;
                    newResource.resource.type = LGFXShaderResourceType_Sampler;
                    newResource.resource.size = 0;

                    newResource.nameStr = name;
                    newResource.states = collections::vector<LGFXFunctionVariable>(results->allocator);
                    results->Insert((usize)binding, newResource);
                }
            }
        }
        JsonElement *inputAttachments = json->GetProperty("inputAttachments");
        if (inputAttachments != NULL)
        {
            for (usize i = 0; i < inputAttachments->arrayElements.length; i++)
            {
                string name = inputAttachments->arrayElements.data[i].GetProperty("name")->GetString(results->allocator);
                u32 index = inputAttachments->arrayElements.data[i].GetProperty("index")->GetUint32();
                u32 set = inputAttachments->arrayElements.data[i].GetProperty("set")->GetUint32();
                u32 binding = inputAttachments->arrayElements.data[i].GetProperty("binding")->GetUint32();

                if (binding > length)
                {
                    length = binding;
                }

                LGFX::ShaderResource *resource = results->Get(binding);
                if (resource != NULL && resource->resource.variableName != NULL)
                {
                    resource->resource.accessedBy = (LGFXShaderInputAccessFlags)((u32)resource->resource.accessedBy | accessedByShaderOfType);
                    name.deinit();
                }
                else
                {
                    ShaderResource newResource;
                    newResource.resource.binding = binding;
                    newResource.resource.set = set;
                    newResource.resource.variableName = name.buffer;
                    newResource.resource.arrayLength = 0;
                    newResource.resource.inputAttachmentIndex = index;
                    newResource.resource.accessedBy = accessedByShaderOfType;
                    newResource.resource.type = LGFXShaderResourceType_InputAttachment;
                    newResource.resource.size = 0;

                    newResource.nameStr = name;
                    newResource.states = collections::vector<LGFXFunctionVariable>(results->allocator);
                    results->Insert((usize)binding, newResource);
                }
            }
        }
        JsonElement *storageBuffers = json->GetProperty("storageBuffers");
        if (storageBuffers != NULL)
        {
            for (usize i = 0; i < storageBuffers->arrayElements.length; i++)
            {
                string name = storageBuffers->arrayElements.data[i].GetProperty("name")->GetString(results->allocator);
                u32 set = storageBuffers->arrayElements.data[i].GetProperty("set")->GetUint32();
                u32 binding = storageBuffers->arrayElements.data[i].GetProperty("binding")->GetUint32();
                u32 mslBinding = storageBuffers->arrayElements.data[i].GetProperty("mslBinding")->GetUint32();

                if (binding > length)
                {
                    length = binding;
                }

                LGFX::ShaderResource *resource = results->Get(binding);
                if (resource != NULL && resource->resource.variableName != NULL)
                {
                    resource->resource.accessedBy = (LGFXShaderInputAccessFlags)((u32)resource->resource.accessedBy | accessedByShaderOfType);
                    name.deinit();
                }
                else
                {
                    ShaderResource newResource;
                    newResource.resource.binding = binding;
                    newResource.resource.set = set;
                    newResource.resource.variableName = name.buffer;
                    newResource.resource.arrayLength = 0;
                    newResource.resource.accessedBy = accessedByShaderOfType;
                    newResource.resource.type = LGFXShaderResourceType_InputAttachment;
                    newResource.resource.size = 0;

                    newResource.nameStr = name;
                    newResource.states = collections::vector<LGFXFunctionVariable>(results->allocator);
                    results->Insert((usize)binding, newResource);
                }
            }
        }

        return length + 1;
    }

    usize CreateShaderFromString(LGFXDevice device, IAllocator allocator, string jsonString, Shader* result)
    {
        *result = Shader(allocator);
        result->device = device;
        ArenaAllocator localArena = ArenaAllocator(GetCAllocator());
        
        JsonElement root;
        usize parseJsonResult = ParseJsonDocument(localArena.AsAllocator(), jsonString, &root);
        if (parseJsonResult != 0)
        {
            localArena.deinit();
            return parseJsonResult;
        }

        JsonElement *computeElement = root.GetProperty("compute");

        if (computeElement != NULL)
        {
            u32 uniformsCount = ParseShaderVariables(computeElement, &result->uniforms, LGFXShaderInputAccess_Compute);
        
            JsonElement *computeSpirv = computeElement->GetProperty("spirv");
            collections::Array<u32> computeSpirvData = collections::Array<u32>(localArena.AsAllocator(), computeSpirv->arrayElements.length);
            for (usize i = 0; i < computeSpirv->arrayElements.length; i++)
            {
                computeSpirvData.data[i] = computeSpirv->arrayElements.data[i].GetUint32();
            }

            LGFXShaderResource *inputResources = (LGFXShaderResource *)malloc(sizeof(LGFXShaderResource) * uniformsCount);
            for (usize i = 0; i < uniformsCount; i++)
            {
                inputResources[i] = result->uniforms.ptr[i].resource;
            }

            LGFXFunctionCreateInfo info;
            info.module1Data = computeSpirvData.data;
            info.module1DataLength = computeSpirvData.length;
            info.module2Data = NULL;
            info.module2DataLength = 0;
            info.uniformsCount = uniformsCount;
            info.uniforms = inputResources;
            LGFXCreateFunction(device, &info);

            free(inputResources);
        }
        else
        {
            JsonElement *vertexElement = root.GetProperty("vertex");
            JsonElement *fragmentElement = root.GetProperty("fragment");

            if (vertexElement != NULL && fragmentElement != NULL)
            {
                u32 max1 = ParseShaderVariables(vertexElement, &result->uniforms, LGFXShaderInputAccess_Vertex);
                u32 max2 = ParseShaderVariables(fragmentElement, &result->uniforms, LGFXShaderInputAccess_Fragment);
                u32 uniformsCount = max1 > max2 ? max1 : max2;

                JsonElement *vertexSpirv = vertexElement->GetProperty("spirv");
                JsonElement *fragmentSpirv = fragmentElement->GetProperty("spirv");

                collections::Array<u32> vertexSpirvData = collections::Array<u32>(localArena.AsAllocator(), vertexSpirv->arrayElements.length);
                collections::Array<u32> fragmentSpirvData = collections::Array<u32>(localArena.AsAllocator(), fragmentSpirv->arrayElements.length);

                for (usize i = 0; i < vertexSpirv->arrayElements.length; i++)
                {
                    vertexSpirvData.data[i] = vertexSpirv->arrayElements.data[i].GetUint32();
                }
                for (usize i = 0; i < fragmentSpirv->arrayElements.length; i++)
                {
                    fragmentSpirvData.data[i] = fragmentSpirv->arrayElements.data[i].GetUint32();
                }

                LGFXShaderResource *inputResources = (LGFXShaderResource *)malloc(sizeof(LGFXShaderResource) * uniformsCount);
                for (usize i = 0; i < uniformsCount; i++)
                {
                    inputResources[i] = result->uniforms.ptr[i].resource;
                }

                LGFXFunctionCreateInfo info;
                info.module1Data = vertexSpirvData.data;
                info.module1DataLength = vertexSpirvData.length;
                info.module2Data = fragmentSpirvData.data;
                info.module2DataLength = fragmentSpirvData.length;
                info.uniformsCount = uniformsCount;
                info.uniforms = inputResources;
                LGFXCreateFunction(device, &info);

                free(inputResources);
            }
            else
            {
                fprintf(stderr, "Failed to detect shader type\n");
                localArena.deinit();
                return 1;
            }
        }

        localArena.deinit();
        return 0;
    }
}