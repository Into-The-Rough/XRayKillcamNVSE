#include "Ammunition.h"

namespace XRay::Ammunition {
Profile Resolve(uint32_t ammo) {
    //exact falloutnv.esm identities: loaded overrides retain these ids, new ammo needs its own profile
    switch(ammo) {
    case 0x7EA27:case 0x121162:case 0x13E439:
        return {.162f,.68f,.22f,true};
    case 0x6B53C:case 0x13E442:case 0x13E443:case 0x140AA0:
        return {.226f,1.72f,.70f,true};
    case 0x8ED02:case 0x13E43C:case 0x140A9E:case 0x15E8EE:
        return {.262f,.98f,.28f,true};
    case 0x2937E:case 0x13E437:case 0x13E438:case 0x15E8ED:
        return {.315f,1.10f,.28f,true};
    case 0x8ECFF:case 0x13E444:case 0x13E445:case 0x140AA1:
        return {.374f,3.15f,1.15f,true};
    case 0x6B53D:case 0x615A8:case 0x121150:case 0x13E43F:case 0x1613FF:
        return {.145f,.95f,.40f,true};
    case 0x4240:case 0x78CC4:case 0x13E440:case 0x13E441:case 0x140A9F:case 0x160C41:
        return {.164f,1.18f,.51f,true};
    case 0x8ED03:case 0x13E43A:case 0x160C40:case 0x176E5C:
        return {.260f,.86f,.27f,true};
    case 0x4241:case 0x13E43B:case 0x140AA8:
        return {.293f,.96f,.29f,true};
    case 0x1429CF:case 0x1613D4:
        return {.367f,1.22f,.34f,true};
    case 0x121133:case 0x13E43D:case 0x13E43E:
        return {.336f,1.55f,.36f,true};
    default:return {};
    }
}
}
