#include "lgfx-astral/ShaderState.hpp"

namespace AstralCanvas
{
    ShaderState::ShaderState()
    {
        this->createInfo = {0};
        this->zoneToInstance = collections::hashmap<AstralCanvas::ShaderStateBindZone, LGFXShaderState>();
    }
    ShaderState::ShaderState(IAllocator allocator, LGFXDevice device, LGFXShaderStateCreateInfo createInfo)
    {
        this->createInfo = createInfo;
        this->zoneToInstance = collections::hashmap<AstralCanvas::ShaderStateBindZone, LGFXShaderState>(allocator, &ShaderStateBindZoneHash, &ShaderStateBindZoneEql);
        this->device = device;
    }
    void ShaderState::deinit()
    {
        free(this->createInfo.vertexDeclarations);
        this->zoneToInstance.deinit();
    }
    
    /// Retrieves or creates an instance of this pipeline for use in the given render program and pass.
    LGFXShaderState ShaderState::GetOrCreateFor(LGFXRenderProgram program, u32 renderPassToUse)
    {
        ShaderStateBindZone zone;
        zone.renderProgram = program;
        zone.renderPass = renderPassToUse;

        LGFXShaderState state = this->zoneToInstance.GetCopyOr(zone, NULL);
        if (state == NULL)
        {
            this->createInfo.forRenderProgram = program;
            this->createInfo.forRenderPass = renderPassToUse;
            state = LGFXCreateShaderState(this->device, &this->createInfo);
        }
        return state;
    }
}