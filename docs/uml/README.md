# UML 图表

本目录存放面向代码阅读的 UML 视图、图源及离线查看页面。
这些图表说明已核对的实现；权威 StarUML 模型仍在项目约定的外部路径。
图表标记的提交是核对基线，修改相关实现时应同步复核图源和源码依据。

## UI subsystem

- [离线图表](ui/ui-components.html)：组件依赖和 LVGL 对象树。
- [组件图源](ui/components.svg)：HSM 直驱被动显示组件。
- [对象图源](ui/objects.svg)：screen、Surface 和页面控件的父子关系。
- [文字说明](../ui-components.md)：组件职责和生命周期。

编辑 SVG 或 HTML 模板后，从仓库根目录重新生成离线页面：

```sh
python3 docs/uml/ui/generate.py
```

生成脚本仅使用 Python 标准库。HTML 内联图形和样式，可以独立离线打开；
SVG 单独保留，供后续修改、引用或导出。现有 `docs/diagrams/` 保持原位。
