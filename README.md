基于b站up无聊的科技和Dr0ii大佬缝合的作业
https://www.bilibili.com/video/BV1m3411J7jZ/?spm_id_from=333.337.search-card.all.click
https://github.com/Dr0ii/EasyWLAN-ESP8266

## 项目文档

本仓库包含完整的毕业设计报告：

📄 **[关于单片机的智能开关设计](docs/关于单片机的智能开关设计.md)** - 完整的中文毕业设计论文（9000+字）

该报告详细描述了基于ESP8266单片机的智能开关系统设计，包括：
- 硬件设计（电源、主控、舵机驱动、保护电路）
- 软件架构（非阻塞状态机、WiFi管理、HTTP/MQTT协议）
- 关键技术（端点自校准、卡滞容错、网络鲁棒性）
- 测试验证与部署指南

### 自动生成Word文档

在 `report-docx` 分支上，CI会自动将Markdown报告转换为Word文档（.docx格式）。

**本地生成Word文档：**

macOS/Linux:
```bash
cd docs
pandoc "关于单片机的智能开关设计.md" \
  --from gfm+yaml_metadata_block \
  --to docx \
  --number-sections \
  --toc \
  --toc-depth=3 \
  --csl gbt-7714-2015-numeric.csl \
  --bibliography references.bib \
  --resource-path=. \
  -o "关于单片机的智能开关设计.docx"
```

Windows (PowerShell):
```powershell
cd docs
pandoc "关于单片机的智能开关设计.md" `
  --from gfm+yaml_metadata_block `
  --to docx `
  --number-sections `
  --toc `
  --toc-depth=3 `
  --csl gbt-7714-2015-numeric.csl `
  --bibliography references.bib `
  --resource-path=. `
  -o "关于单片机的智能开关设计.docx"
```

**前提条件：** 安装 [Pandoc](https://pandoc.org/installing.html)
- macOS: `brew install pandoc`
- Windows: `choco install pandoc` 或 `scoop install pandoc`
- Linux: `sudo apt-get install pandoc`

**注意：** Word默认边距为2.54cm，符合论文格式要求（接近标准的2.5cm边距）。

