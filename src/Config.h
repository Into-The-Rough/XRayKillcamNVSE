#pragma once

namespace XRay::Config {
//reads every setting from the ini, a missing or malformed key takes its default
void Load(const char* path);
}
