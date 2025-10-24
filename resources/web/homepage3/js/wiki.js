var cardData = [
  {
    "title": "Q2",
    "img": "img/printer_q2.png",
    "link": "Q2"
  },
  {
    "title": "X-Plus 4",
    "img": "img/printer_xplus4.png",
    "link": "X-Plus4"
  },
  {
    "title": "Q1 Pro",
    "img": "img/printer_q1pro.png",
    "link": "Q1-Pro"
  },
  {
    "title": "X-Max 3",
    "img": "img/printer_xmax3.png",
    "link": "X-Max3"
  },
  {
    "title": "X-Plus 3",
    "img": "img/printer_xplus3.png",
    "link": "X-Plus3"
  },
  {
    "title": "X-Smart 3",
    "img": "img/printer_xsmart3.png",
    "link": "X-Smart3"
  },
  {
    "title": "QIDI Studio",
    "img": "img/QIDIStudio.png",
    "link": "software/qidi-studio"
  },
];

// var youtubeData = [
//   {
//     "id": "8TQCRVS72Us",
//     "title": "Intro: Overview of Bambu Studio"
//   },
//   {
//     "id": "AdHUVQiVeDI",
//     "title": "Episode 1: Set Up Your Environment"
//   },
//   {
//     "id": "LDrCjCPYbhE",
//     "title": "Episode 2: Import 3D Printing Models"
//   },
//   {
//     "id": "3MYKsbd7k0c",
//     "title": "Episode 3: Adjust the Models to Be Printable - Part 1"
//   },
//   {
//     "id": "KcMBajgOzS0",
//     "title": "Episode 4: Adjust the Models to Be Printable - Part 2"
//   },
//   {
//     "id": "4W4vczKsiX8",
//     "title": "Episode 5: Setup for Filaments"
//   },
//   {
//     "id": "lQ4ySwPQeY4",
//     "title": "Episode 6: Color Models for Multi-Color Prints"
//   },
//   {
//     "id": "3tft_Ahkd_w",
//     "title": "Episode 7: Divide and Print Your Models on Multiple Plates"
//   },
//   {
//     "id": "ZH9xqdQYXsI",
//     "title": "Episode 8: The Basic Quality Settings"
//   },
//   {
//     "id": "FRLympzHsWY",
//     "title": "Episode 9: The Basic Strength Settings"
//   },
//   {
//     "id": "1QJ2UKDamzc",
//     "title": "Episode 10: The Basic Support Settings"
//   },
//   {
//     "id": "EHZtnnQLtGs",
//     "title": "Episode 11: The Basic Other Settings"
//   },
//   {
//     "id": "DWna4Mwjs5c",
//     "title": "Episode 12: Check Your Sliced Model in Preview"
//   },
//   {
//     "id": "pnd1CZ-Dp34",
//     "title": "Episode 13: Send a Print Job to Your Printer"
//   },
//   {
//     "id": "SZCWxYeMZro",
//     "title": "Episode 14: During the Print"
//   },
// ];

var topicData = [
  {
    "title": "Quick Start",
    "zhcn-title": "快速开始",
    "children": [
      {
        "title": "QIDI Studio Quick Start Guide",
        "zhcn-title": "QIDI Studio 快速上手教程",
        "link": "software/qidi-studio/quick-guide"
      },
      {
        "title": "Connect device and Fluidd",
        "zhcn-title": "设备连接与Fluidd页面",
        "link": "software/qidi-studio/fluidd"
      },
      // {
      //   "title": "QIDI Box使用教程",
      //   "zhcn-title": "QIDI Box User Guide",
      //   "link": "software/qidi-studio/use-box-guide"
      // },
      // {
      //   "title": "QIDI Studio Multi Plate Printing Guide",
      //   "zhcn-title": "QIDI Studio 多盘打印指南",
      //   "link": "software/qidi-studio/multi-plate-printing"
      // }, 
    ]
  },
  {
    "title": "Gizmo",
    "zhcn-title": "顶部工具栏",
    "children": [
      {
        "title": "Variable Layer Height",
        "zhcn-title": "可变层高",
        "link": "software/qidi-studio/toolbar/variable-layer-height"
      },
      {
        "title": "Cut Tool",
        "zhcn-title": "切割工具",
        "link": "software/qidi-studio/toolbar/cut-tool"
      },
      {
        "title": "Auto Orientation",
        "zhcn-title": "自动朝向",
        "link": "software/qidi-studio/toolbar/autoOrient"
      },
      {
        "title": "Split to Objects/Parts",
        "zhcn-title": "拆分为对象/零件",
        "link": "software/qidi-studio/toolbar/split-obj-part"
      },
      {
        "title": "Support Painting Guide",
        "zhcn-title": "支撑绘制",
        "link": "software/qidi-studio/toolbar/support-paint-guide"
      },
      {
        "title": "3D Text",
        "zhcn-title": "添加 3D 文本",
        "link": "software/qidi-studio/toolbar/3dtext"
      }
    ]
  },
  {
    "title": "Right-click Tool",
    "zhcn-title": "右键工具",
    "children": [
      {
        "title": "Fix Model",
        "zhcn-title": "修复模型",
        "link": "software/qidi-studio/toolbar/fix-model"
      },
      {
        "title": "Simplify Model",
        "zhcn-title": "简化模型",
        "link": "software/qidi-studio/toolbar/simplify-model"
      },
      {
        "title": "Negative Part",
        "zhcn-title": "负零件",
        "link": "software/qidi-studio/toolbar/negative-part"
      },
      {
        "title": "Modifier Operation Guide",
        "zhcn-title": "修改器操作指南",
        "link": "software/qidi-studio/toolbar/add-modify"
      }
    ]
  },
  // {
  //   "title": "Multi-material Printing",
  //   "zhcn-title": "多材料打印",
  //   "children": [
  //     {
  //       "title": "Multi-Color Printing",   
  //       "zhcn-title": "多色打印指南",
  //       "link": "software/qidi-studio/multi-color-printing/multi-color-printing"
  //     },
  //     {
  //       "title": "Color Painting Tool",   
  //       "zhcn-title": "涂色工具使用指南",
  //       "link": "software/qidi-studio/multi-color-printing/color-painting-tool"
  //     },
  //     {
  //       "title": "Reduce Waste during Filament Change",   
  //       "zhcn-title": "减少多色打印时的材料浪费",
  //       "link": "software/qidi-studio/multi-color-printing/reduce-wasting-during-filament-change"
  //     },
  //   ]
  // },
  {
    "title": "Print Settings",
    "zhcn-title": "打印设置",
    "children": [
      // {
      //   "title": "Special Slicing Mode in Bambu Studio",
      //   "zhcn-title": "Bambu Studio 特殊切片模式",
      //   "link": "software/bambu-studio/special-slicing-modes"
      // },
      {
        "title": "How to Create Custom Preset",
        "zhcn-title": "创建自定义预设",
        "link": "software/qidi-studio/print-settings/custom-filament"
      },
      {
        "title": "Seam Settings",
        "zhcn-title": "接缝设置",
        "link": "software/qidi-studio/print-settings/seam"
      },
      {
        "title": "Support settings in Bambu Studio",
        "zhcn-title": "支撑耗材与支撑功能的介绍",
        "link": "software/qidi-studio/print-settings/support"
      },
      {
        "title": "Brim",
        "zhcn-title": "Brim",
        "link": "software/qidi-studio/print-settings/brim"
      },
    ]
  },
  {
    "title": "Calibration",
    "zhcn-title": "流量校准",
    "children": [
      // {
      //   "title": "Flow Rate Calibration",
      //   "zhcn-title": "流量比例",
      //   "link": "software/bambu-studio/calibration_flow_rate"
      // },
      // {
      //   "title": "Flow Dynamics Calibration",
      //   "zhcn-title": "动态流量校准",
      //   "link": "software/bambu-studio/calibration_pa"
      // }
      {
        "title": "Calibration",
        "zhcn-title": "校准",
        "link": "software/qidi-studio/calibration"
      }
    ]
  },
  {
    "title": "Troubleshooting",
    "zhcn-title": "故障排除",
    "children": [
      {
        "title": "Failed to send print files or failed to connect printer",
        "zhcn-title": "无法发送打印文件或无法连接打印机",
        "link": "software/qidi-studio/troubleshooting/connect-send-problem"
      },
      {
        "title": "QIDI Studio crashes/freezes troubleshooting guide",
        "zhcn-title": "QIDI Studio 崩溃/冻结故障排除指南",
        "link": "software/qidi-studio/troubleshooting/crash_freeze_guide"
      },
      {
        "title": "Export QIDI Studio software log",
        "zhcn-title": "导出QIDI PC软件日志",
        "link": "software/qidi-studio/troubleshooting/export-logs"
      }
    ]
  }
];

var $prev;
var $next;

var video_prev;
var video_next;

function OnInit() {
  createCardHTML();
  createVideoHTML();
  if (IsChinese())
    $("#tutorial_block").hide();
  createTopicHTML();
  TranslatePage();

  $prev = $('#academy_Left_Btn');
  $next = $('#academy_Right_Btn');

  $prev.on('click', () => scrollByStep(-1));
  $next.on('click', () => scrollByStep(+1));
  $('#academy_content').on('scroll', updateButtons);
  $(window).on('resize', updateButtons);
  updateButtons();

  $video_prev = $('#tutorial_Left_Btn');
  $video_next = $('#tutorial_Right_Btn');
  $video_prev.on('click', () => scrollVideoByStep(-1));
  $video_next.on('click', () => scrollVideoByStep(+1));
  $('#tutorial_content').on('scroll', updateVideoButtons);
  $(window).on('resize', updateVideoButtons);
  updateVideoButtons();

  $("#Wiki_Search_Input").on("input", debounce(function() {
    let keyword = $(this).val();
    searchWiki(keyword);
  }, 500));
  $("#Wiki_Search_Input").on("focus", function () {
    if (this.value.trim().length) {
      $(this).trigger("input");
    }
  });

  // $(".searchResult").on("click", function(e) {
  //   console.log(e);
  // });
  $(document).on('click', function (e) {
    if (!$(e.target).closest('.search').length) {
      $('#search_result_area').empty();
    }
  });
}

function debounce(fn, delay){
  let t;
  return function(...args){
    clearTimeout(t);
    t = setTimeout(() => fn.apply(this, args), delay);
  };
}

// search

function searchWiki(keyword) {
  var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="search_wiki";
  tSend['data']={};
  tSend['data']['keyword']=keyword;

	SendWXMessage( JSON.stringify(tSend) );
}

function updateSearchResult(result) {
  let data = result["pages"]["search"];
  $('#search_result_area').empty();
  if (data["totalHits"] > 0) {
    data["results"].forEach(element => {
      if (IsChinese()) {
        if (element["locale"] != "zh")
          return;
      }else {
        if (element["locale"] != "en")
          return;
      }
      let html = `<div class="searchResult" onclick="openWikiUrl('${element.path}')">${element.title}</div>`;
      $('#search_result_area').append(html);
    });
  }
}

//--------------- Academy Cards -------------------

function createCardHTML() {
  for (let i = 0; i < cardData.length; i++) {
    let html = `<div class="card" data-idx="${i}" onclick="openAcademyUrl('${cardData[i].link}')">
                  <img class="cardImg" src="${cardData[i].img}" />
                  <div class="cardTitle">${cardData[i].title}</div>
                </div>`;
    $('#academy_Card_Content').append(html)
  }
}

function stepCardSize() {
  const $first = $('#academy_Card_Content').children().first();
  if ($first.length === 0) return 0;
  let gap = 16;
  return $first.outerWidth(true) + gap;
}

function maxScrollLeft() {
  var a = $('#academy_Card_Content').outerWidth(true);
  var b = $('#academy_content').innerWidth();
  return Math.max(0, $('#academy_Card_Content').outerWidth(true) - $('#academy_content').innerWidth());
}

function clampScroll(x) {
  const max = maxScrollLeft();
  return Math.min(Math.max(0, x), max);
}

function updateButtons() {
  const x = Math.round($('#academy_content').scrollLeft());
  const max = Math.round(maxScrollLeft());
  $prev.prop('disabled', x <= 0);
  $next.prop('disabled', x >= max);
}

function scrollByStep(dir) {
  const step = stepCardSize();
  if (!step) return;
  const current = $('#academy_content').scrollLeft();
  const target = clampScroll(current + dir * step);
  $('#academy_content').stop(true, false).animate({ scrollLeft: target }, 260, 'swing', updateButtons);
}

function openAcademyUrl(path)
{
  let open_url = "";
  if (IsChinese()){
    open_url = "https://wiki.qidi3d.com/zh/";
  }else{
    let strLang=langStringTransfer();
    open_url = "https://wiki.qidi3d.com/en/";
  }
  open_url += path;
  OpenUrlInLocalBrowser(open_url);
}


// ---------------- Tutorials -------------------

function createVideoHTML() {
  // let academyData = youtubeData;
  // for (let i = 0; i < academyData.length; i++) {
  //   let html = `<div class="videoCard" onclick="openVideoUrl('${academyData[i].id}')">
  //                 <img class="videoThumbnail" src="img/${academyData[i].id}.jpg" />
  //                 <div class="videoTitle TextS1">${academyData[i].title}</div>
  //               </div>`;
  //   $('#tutorial_Card_Content').append(html)
  // }
}

function stepVideoCardSize() {
  const $first = $('#tutorial_Card_Content').children().first();
  if ($first.length === 0) return 0;
  let gap = 16;
  return $first.outerWidth(true) + gap;
}

function maxVideoScrollLeft() {
  var a = $('#tutorial_Card_Content').outerWidth(true);
  var b = $('#tutorial_content').innerWidth();
  return Math.max(0, $('#tutorial_Card_Content').outerWidth(true) - $('#tutorial_content').innerWidth());
}

function videoClampScroll(x) {
  const max = maxVideoScrollLeft();
  return Math.min(Math.max(0, x), max);
}

function updateVideoButtons() {
  const x = Math.round($('#tutorial_content').scrollLeft());
  const max = Math.round(maxVideoScrollLeft());
  $video_prev.prop('disabled', x <= 0);
  $video_next.prop('disabled', x >= max);
}

function scrollVideoByStep(dir) {
  const step = stepVideoCardSize();
  if (!step) return;
  const current = $('#tutorial_content').scrollLeft();
  const target = videoClampScroll(current + dir * step);
  $('#tutorial_content').stop(true, false).animate({ scrollLeft: target }, 260, 'swing', updateVideoButtons);
}

function openVideoUrl(path)
{
  let open_url = "https://www.youtube.com/watch?v=";
  open_url += path;
  OpenUrlInLocalBrowser(open_url);
}

// ---------------- topic -------------------

function createTopicHTML() {
  for (let i = 0; i < topicData.length; i++) {
    let title;
    if (IsChinese()){
      title = topicData[i]['zhcn-title'];
    } else {
      title = topicData[i].title;
    }
    let html = `<div class="topicCard">
          <div class="topicTitle">${title}</div>
          <ul>`
    for (let j=0; j < topicData[i].children.length; j++) {
      let child_title;
      if (IsChinese()){
        child_title = topicData[i].children[j]['zhcn-title'];
      } else {
        child_title = topicData[i].children[j].title;
      }
      html += `<li onclick="openWikiUrl('${topicData[i].children[j].link}')">${child_title}</li>`;
    }
    html += `</ul></div>`;
    $('#topic_content').append(html)
  }
}

function openWikiUrl(path)
{
  let open_url = "https://wiki.qidi3d.com/"
  if (IsChinese()){
    open_url += "zh/";
  }else{
    open_url += "en/";
  }
  open_url += path;

  var tSend={};
	tSend['sequence_id']=Math.round(new Date() / 1000);
	tSend['command']="userguide_wiki_open";
	tSend['data']={};
	tSend['data']['url']=open_url;
	
	SendWXMessage( JSON.stringify(tSend) );	
}

// --------------------common ----------------

function IsChinese()
{
	let strLang=GetQueryString("lang");
	if(strLang!=null)
	{}else{
		strLang=localStorage.getItem(LANG_COOKIE_NAME);
	}
	
	if(strLang!=null)
		return strLang.includes('zh')
	else
		return false;
}

function langStringTransfer()
{
  let strLang=GetQueryString("lang");
  if(strLang==null)
    strLang=localStorage.getItem(LANG_COOKIE_NAME);
  
  if(strLang.includes('zh')){
    return 'zh';
  }else if(strLang.includes('en')){
    return 'en';
  }else if(strLang.includes('fr')){
    return 'fr-fr';
  }else if(strLang.includes('de')){
    return 'de-de';
  }else if(strLang.includes('es')){
    return 'es-mx';
  }else if(strLang.includes('it')){
    return 'it-it';
  }else if(strLang.includes('ja')){
    return 'ja-jp';
  }else if(strLang.includes('ko')){
    return 'ko-kr';
  }else if(strLang.includes('pt')){ 
    return 'pt-br';
  }else if(strLang.includes('nl')){
    return 'nl-nl';
  }else{
    return 'en';
  }
}

function HandleStudio( pVal )
{
  let strCmd = pVal['command'];
  if(strCmd=='search_wiki_get')
	{
		updateSearchResult(pVal['data']);
	}
}

//---------------Global-----------------
window.postMessage = HandleStudio;