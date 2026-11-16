# Extra firmware code that lives in this repo.
if(CONFIG_ZMK_LAYER_LIGHTS)
  target_sources(app PRIVATE src/layer_lights.c)
endif()
