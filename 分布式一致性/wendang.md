
# 自定义转码参数

##文档转码
### 水印
水印和转码模板加水印相同，放在preset.Watermark中，支持多个水印， 目前不支持动态水印，不建议开放动态水印相关参数，不支持水印格式为gif
 

### 自定义分辨率

自定义参数保存在extensionFileds中，json格式为：
```json
{
    "filetranscode":{
        "image_infos":[
            {
                "h":str,
                "w":str,
                "dpi": int32_t,
            },
        ]
    }
}
```
其中h为高，w为宽，h，w为整数或者是auto，但是h，w不能同时设置为auto
当extensionFileds中包含image_infos时，覆盖原有的FileTranscode中的FileTransJob.outputsInfo字段
### 回调字段
VideoCompleted 中fileInfo字段增加version字段，version为0，或者不存在是为老的回调，version为1时使用，使用extends_info字段
```json
{
	"pic_infos":[
		{
			"h":int32_t,
			"w":int32_t,
			"dpi":int32_t,
			"outputkey":string,
			"region":int32_t
			"bucket":string,
			"pic_size":int32_t,
			"origin_h":string,
			"origin_w":string,
		},
	]
}
```
### 文件名称规则
* 使用ImageQuolity：{$outputkey}_{%d}\_{%d}.(png|jpg),第一个%d标示质量为1,2,3，第二%d标示第几页
* 使用自定义宽高时：{$outputkey}_{%s}\_{%s}\_{%d}.(png|jpg),第一个%s标示高度，是传入的高度的原始字符串，第二个%s标示宽，是传入的宽度的原始字符串，第三个%d标示第几页
 
### 配置改动
worker中增加fileTransMergeImageInfo，标识在extensionFileds中包含image_infos时是否继续处理原有的ImageQuality，
* 0，不处理，为默认配置
* 1，同时处理extensionFileds中的image_infos和ImageQuality标示的图片质量
 
## 视频转码

### 文字水印
```json

```