Shader "Custom/Raycaster"
{
    Properties
    {
        _MainTex("Texture", 2D) = "white" {}
        _FOV("Field of View", Range(30, 120)) = 60
        _RayCount("Ray Count", Int) = 320
    }
    SubShader
    {
        Pass
        {
            CGPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #pragma target 3.0

            #include "UnityCG.cginc"

            struct appdata
            {
                float4 vertex : POSITION;
                float2 uv : TEXCOORD0;
            };

            struct v2f
            {
                float2 uv : TEXCOORD0;
                float4 vertex : SV_POSITION;
            };

            sampler2D _MainTex;
            sampler2D _WallTexture1, _WallTexture2, _WallTexture3, _WallTexture4, _WallTexture5;
            float4 _MainTex_ST;
            float _FOV;
            int _RayCount;

            float4x4 _CameraMatrix;

            v2f vert (appdata v)
            {
                v2f o;
                o.vertex = UnityObjectToClipPos(v.vertex);
                o.uv = TRANSFORM_TEX(v.uv, _MainTex);
                return o;
            }

            float4 frag (v2f i) : SV_Target
            {
                float2 uv = i.uv * 2.0 - 1.0;
                float aspectRatio = _ScreenParams.x / _ScreenParams.y;
                uv.x *= aspectRatio;

                float angle = atan(uv.x, 1.0);
                float distance = length(uv);

                float4 color = tex2D(_MainTex, i.uv);

                int wallIndex = int(frac(uv.x * 5.0) * 5.0);
                
                if (wallIndex == 0)
                    color = tex2D(_WallTexture1, uv * 0.1);
                else if (wallIndex == 1)
                    color = tex2D(_WallTexture2, uv * 0.1);
                else if (wallIndex == 2)
                    color = tex2D(_WallTexture3, uv * 0.1);
                else if (wallIndex == 3)
                    color = tex2D(_WallTexture4, uv * 0.1);
                else
                    color = tex2D(_WallTexture5, uv * 0.1);

                float brightness = 1.0 - (distance * 0.5);
                color.rgb *= max(brightness, 0.2);

                return color;
            }
            ENDCG
        }
    }
}
