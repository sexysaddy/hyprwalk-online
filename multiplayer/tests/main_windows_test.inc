 // Stage 3C: actual adapter methods, actual local rasters and TCP/UDP, mock window wrappers.
 {
 auto a=std::make_shared<WindowDouble>(), secret=std::make_shared<WindowDouble>(), blocked=std::make_shared<WindowDouble>();
 a->m_title="PUBLIC A";secret->m_title="PRIVATE A";blocked->m_title="DO NOT SHARE";blocked->m_ruleApplicator->privacy.value=true;
 auto layer=std::make_shared<WindowDouble>(),popup=std::make_shared<WindowDouble>(),dialog=std::make_shared<WindowDouble>();dialog->parent=true;
 auto b=std::make_shared<WindowDouble>(),b2=std::make_shared<WindowDouble>();b->m_title="PUBLIC B";b2->m_title="PUBLIC B2";
 h.m_panels={{PANEL_WINDOW,a},{PANEL_WINDOW,secret},{PANEL_WINDOW,blocked},{PANEL_LAYER,layer},{PANEL_POPUP,popup},{PANEL_WINDOW,dialog}};
 c.m_panels={{PANEL_WINDOW,b},{PANEL_WINDOW,b2}};
 h.multiplayerWindowsUpdate(true);c.multiplayerWindowsUpdate(true);
 auto& hw=h.m_multiplayerAdapter.windows;auto& cw=c.m_multiplayerAdapter.windows;
 require(hw.ready()&&cw.ready()&&hw.local().size()==2&&cw.local().size()==2);
 require(hw.mode()==mp::SharingMode::Off&&cw.mode()==mp::SharingMode::Off);
 for(int i=0;i<20;++i)pump();require(hw.remote().empty()&&cw.remote().empty());
 auto idA=hw.local().front().id;auto idB=cw.local().front().id;
 require(mp::windowMenuAction(hw.generationToken()+":selected:0",hw).empty());
 require(mp::windowMenuAction(hw.generationToken()+":select:"+std::to_string(idA),hw).empty());
 require(!cw.setMode(mp::SharingMode::All));require(cw.setMode(mp::SharingMode::All,true));
 h.multiplayerWindowPreferenceChanged();c.multiplayerWindowPreferenceChanged();
 wait([&]{h.multiplayerWindowsUpdate();c.multiplayerWindowsUpdate();return hw.remote().size()==2&&cw.remote().size()==1;});
 require(cw.remote()[0].metadata.title=="PUBLIC A");require(hw.remote()[0].metadata.title=="PUBLIC B");
 SFrameParams windowsFrame;h.multiplayerWindowsFrame(windowsFrame);require(windowsFrame.remoteWindows&&windowsFrame.remoteWindows->size()==2);
 const auto panelSerial=windowsFrame.remoteWindows->front().panel->serial;
 const auto windowPage="online-windows:item:"+hw.generationToken()+":"+std::to_string(idB);
 h.m_menu.show(windowPage);h.m_menu.update(.1f,1920,1080,1.f);
 require(std::ranges::none_of(h.m_menu.page().items,[](const auto& item){return item.dial;}));
 h.m_menu.hide();
 h.multiplayerWindowsFrame(windowsFrame);require(windowsFrame.remoteWindows->front().panel->serial==panelSerial);
 require(hw.hide(idB,true));h.multiplayerWindowPreferenceChanged();h.multiplayerWindowsFrame(windowsFrame);
 require(windowsFrame.remoteWindows->size()==1&&hw.findRemote(idB)->receive.hidden);
 require(cw.mode()==mp::SharingMode::All&&cw.publication().windows.size()==2);
 require(hw.hide(idB,false));h.multiplayerWindowPreferenceChanged();require(!hw.findRemote(idB)->receive.hidden);
 // A left click on a remote panel selects it for controls; no remote-app input is sent.
 c.m_menu.hide();require(c.multiplayerWindowsAim(1e30f));require(c.m_remoteWindowHit->key.window==idA);
 require(c.multiplayerWindowButton(BTN_LEFT_,true));require(cw.chosen()==idA);
 require(c.m_aimed==-1&&c.m_aimSurface.expired());require(c.multiplayerWindowWheel(1));
 auto pubBefore=hw.publication().windows;require(pubBefore.size()==1&&hw.selected(idA));
 // Geometry belongs to the publisher and reuses the receiver's panel image.
 h.m_panels[0].pose.origin.x=3;h.m_panels[0].box.w=1200;h.multiplayerWindowsUpdate(true);
 wait([&]{return cw.findRemote(idA)&&cw.findRemote(idA)->metadata.geometry.width==3;});require(cw.findRemote(idA)->metadata.geometry.origin[0]==3);
 // Late noScreenShare must withdraw metadata before reading any new private title.
 a->m_ruleApplicator->privacy.value=true;a->m_title="SECRET AFTER RULE";h.multiplayerWindowsUpdate(true);
 wait([&]{return cw.remote().empty();});require(!hw.selected(idA));
 a->m_ruleApplicator->privacy.value=false;h.multiplayerWindowsUpdate(true);require(hw.publication().windows.empty());
 require(hw.local().front().id!=idA); // remapped/private source gets a fresh registry identity
 // Lock resets consent, but neither direction can change the other's receiving settings.
 lockManager.locked=true;c.multiplayerWindowsUpdate(true);lockManager.locked=false;
 require(cw.mode()==mp::SharingMode::Off);wait([&]{return hw.remote().empty();});
 h.m_panels.clear();c.m_panels.clear();hw.revoke();cw.revoke();h.multiplayerWindowsUpdate(true);c.multiplayerWindowsUpdate(true);
 h.enterOnline();c.enterOnline();
 std::cout<<"PASS exact window adapters: privacy filters, both publishers, video-only menu, local hide, placeholder, hit controls, pose/size, rule withdrawal, fresh IDs, lock reset\n";
 }
