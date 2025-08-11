#include "RenderDevice.hpp"
#include "Core/Assert.hpp"

bool Nightbloom::RenderDevice::SupportsFeature(const std::string& featureName) const
{
	UNUSED(featureName);
	return false;
}
