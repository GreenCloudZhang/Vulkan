#version 450

layout(binding=1)uniform sampler2D randomMap;
layout(binding=2)uniform sampler2D inColorMap;

layout(binding=0)uniform Params
{
	vec4 randomParams;
}params;
layout(location=0)in vec2 inUV;
layout(location=0)out vec4 outColor;

/////////
//      stochastic_cat_rom
//full_cat_rom    stochastic bilinear
//      normal bilinear

float rand(float co) { return fract(sin(co*(91.3458)) * 47453.5453); }
float rand(vec2 co){ return fract(sin(dot(co.xy ,vec2(12.9898,78.233))) * 43758.5453); }

vec4 cat_rom(in float t) {
    return vec4(t*((2.0-t)*t - 1.0) / 2.0, (t*t*(3.0*t - 5.0) + 2.0) / 2.0,
           t*((4.0 - 3.0*t)*t + 1.0) / 2.0, (t-1.0)*t*t / 2.0);
}


vec3 full_cat_rom(in vec2 uv, in vec2 rand) {
	ivec2 texSize = textureSize(inColorMap, 0);
    vec2 uv_full = uv * texSize.xy - 0.5;
    vec2 left_top = floor(uv_full);
    ivec2 left_top_i = ivec2(uv_full);
    vec2 fract_part = uv_full - left_top;
    left_top = (left_top + 0.5) / texSize.xy;
    vec4 cr_x = cat_rom(fract_part.x);
    vec4 cr_y = cat_rom(fract_part.y);

    vec3 accum = vec3(0.0);
    for (int dy = -1; dy <= 2; dy +=1) {
        for (int dx = -1; dx <= 2; dx +=1) {
            accum += texture(inColorMap, left_top + vec2(dx, dy)/ texSize.xy).xyz * cr_x[dx+1]  * cr_y[dy+1];
        }
    }

    return accum;
}


vec2 sample_select_neg(in float[16] w, in float rand) {
    float wa[] = float[](abs(w[1]), abs(w[2]), abs(w[4]), abs(w[7]), abs(w[8]), abs(w[11]), abs(w[13]), abs(w[14]));
    float sum = (wa[0]+wa[1]+wa[2]+wa[3]+wa[4]+wa[5]+wa[6]+wa[7]);
    float ws_cdf[] = float[](wa[0]/sum,
                           (wa[0]+wa[1])/sum,
                           (wa[0]+wa[1]+wa[2])/sum,
                           (wa[0]+wa[1]+wa[2]+wa[3])/sum,
                           (wa[0]+wa[1]+wa[2]+wa[3]+wa[4])/sum,
                           (wa[0]+wa[1]+wa[2]+wa[3]+wa[4]+wa[5])/sum,
                           (wa[0]+wa[1]+wa[2]+wa[3]+wa[4]+wa[5]+wa[6])/sum, 
                           (wa[0]+wa[1]+wa[2]+wa[3]+wa[4]+wa[5]+wa[6]+wa[7])/sum);
    if (rand <= ws_cdf[0])
        return vec2(1, -sum);
    if (rand <= ws_cdf[1])
        return vec2(2, -sum);
    if (rand <= ws_cdf[2])
        return vec2(4, -sum);
    if (rand <= ws_cdf[3])
        return vec2(7, -sum);
    if (rand <= ws_cdf[4])
        return vec2(8, -sum);
    if (rand <= ws_cdf[5])
        return vec2(11, -sum);
    if (rand <= ws_cdf[6])
        return vec2(13, -sum);
    return vec2(14, -sum);

}

vec2 sample_select_pos(in float[16] w, in float rand) {
    float wa[] = float[](abs(w[0]), abs(w[3]), abs(w[5]), abs(w[6]), abs(w[9]), abs(w[10]), abs(w[12]), abs(w[15]));
    float sum = (wa[0]+wa[1]+wa[2]+wa[3]+wa[4]+wa[5]+wa[6]+wa[7]);
    float ws_cdf[] = float[](wa[0]/sum,
                           (wa[0]+wa[1])/sum,
                           (wa[0]+wa[1]+wa[2])/sum,
                           (wa[0]+wa[1]+wa[2]+wa[3])/sum,
                           (wa[0]+wa[1]+wa[2]+wa[3]+wa[4])/sum,
                           (wa[0]+wa[1]+wa[2]+wa[3]+wa[4]+wa[5])/sum,
                           (wa[0]+wa[1]+wa[2]+wa[3]+wa[4]+wa[5]+wa[6])/sum, 
                           (wa[0]+wa[1]+wa[2]+wa[3]+wa[4]+wa[5]+wa[6]+wa[7])/sum);
    if (rand <= ws_cdf[0])
        return vec2(0, sum);
    if (rand <= ws_cdf[1])
        return vec2(3, sum);
    if (rand <= ws_cdf[2])
        return vec2(5, sum);
    if (rand <= ws_cdf[3])
        return vec2(6, sum);
    if (rand <= ws_cdf[4])
        return vec2(9, sum);
    if (rand <= ws_cdf[5])
        return vec2(10, sum);
    if (rand <= ws_cdf[6])
        return vec2(12, sum);
    return vec2(15, sum);
}

vec3 stochastic_cat_rom(in vec2 uv, in vec2 rand) {
	ivec2 texSize = textureSize(inColorMap, 0);
    vec2 uv_full = uv * texSize.xy - 0.5;
    vec2 left_top = floor(uv_full);
    ivec2 left_top_i = ivec2(uv_full);
    vec2 fract_part = uv_full - left_top;
    left_top = (left_top + 0.5) / texSize.xy;
    vec4 cr_x = cat_rom(fract_part.x);
    vec4 cr_y = cat_rom(fract_part.y);
  
    float[16] all_samp = float[16](cr_x[0]*cr_y[0], cr_x[1]*cr_y[0], cr_x[2]*cr_y[0], cr_x[3]*cr_y[0],
                     cr_x[0]*cr_y[1], cr_x[1]*cr_y[1], cr_x[2]*cr_y[1], cr_x[3]*cr_y[1],
                     cr_x[0]*cr_y[2], cr_x[1]*cr_y[2], cr_x[2]*cr_y[2], cr_x[3]*cr_y[2],
                     cr_x[0]*cr_y[3], cr_x[1]*cr_y[3], cr_x[2]*cr_y[3], cr_x[3]*cr_y[3]);
    vec2 sample_ind0 = sample_select_neg(all_samp, rand.x);
    vec2 sample_ind1 = sample_select_pos(all_samp, rand.y);

    float dx0 = float(int(sample_ind0)%4)-1.0;
    float dx1 = float(int(sample_ind1)%4)-1.0;
    float dy0 = float(int(sample_ind0)/4)-1.0;
    float dy1 = float(int(sample_ind1)/4)-1.0;
    
    vec3 accum = vec3(0.0);
    accum += texelFetch(inColorMap, left_top_i + ivec2(dx0, dy0), 0).xyz * sample_ind0.y;
    accum += texelFetch(inColorMap, left_top_i + ivec2(dx1, dy1), 0).xyz * sample_ind1.y;
   

    return accum;
}


vec3 stochastic_bilin(in vec2 uv, in vec2 rand) {
  ivec2 texSize = textureSize(inColorMap, 0);
  vec2 uv_full = uv * texSize.xy - 0.5;
  vec2 left_top = floor(uv_full);
  ivec2 left_top_i = ivec2(uv_full);
  vec2 fract_part = uv_full - left_top;
  left_top = (left_top + 0.5) / texSize.xy;
  
  vec4 ws = vec4((1.0-fract_part.x)*(1.0-fract_part.y),
                 fract_part.x*(1.0-fract_part.y),
                 (1.0-fract_part.x)*fract_part.y,
                 fract_part.x*fract_part.y);
  ws = vec4(ws.x, ws.x+ws.y, ws.x+ws.y+ws.z, ws.x+ws.y+ws.z+ws.w);
  ws /= ws.w;
  
  int sel = 3;
  if (rand.x < ws.x)
  {
      sel = 0;
  } else if (rand.x < ws.y) {
      sel = 1;
  } else if (rand.x < ws.z) {
      sel = 2;
  }
  
  float dx = (sel == 1 || sel == 3) ? 1.0 : 0.0;
  float dy = (sel == 2 || sel == 3) ? 1.0 : 0.0;

  return texelFetch(inColorMap, left_top_i + ivec2(dx, dy), 0).xyz;  
}


void main()
{
	vec4 random = texture(randomMap,inUV+vec2(rand(params.randomParams.x),rand(params.randomParams.y)));
    vec3 col = inUV.x < inUV.y ? full_cat_rom(inUV, random.xy) : stochastic_cat_rom(inUV, random.xy);

	if(1.0-inUV.x < inUV.y)
	{
		col = texture(inColorMap, inUV).xyz;
		if(inUV.x > inUV.y)
		{
			col = stochastic_bilin(inUV, random.xy);
		}
	}

	//split
	if(abs(1.0-inUV.x-inUV.y)<0.005)
	    col=vec3(1,1,1);
	if(abs(inUV.x-inUV.y)<0.005)
	    col=vec3(1,1,1);
	
	outColor=vec4(col,1.0);
}
