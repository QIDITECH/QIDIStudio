//y30
function OnHomeInit()
{
	//-----Official-----
    TranslatePage();
    UpdateConnectionImage();
}

//y30
function UpdateConnectionImage()
{
    let img = document.getElementById("connectionImg");
    if (!img) return;
    let lang = GetCurrentLang();
    img.src = (lang === "zh_CN") ? "link_connection_CN.png" : "link_connection_other.png";
}


//---------------Global-----------------
window.postMessage = HandleStudio;
