"""Read only the public focused editor inside the explicit private browser."""
import gi, json, os, sys
from pathlib import Path
gi.require_version('Atspi','2.0')
from gi.repository import Atspi
runtime=Path(os.environ['XDG_RUNTIME_DIR'])
assert runtime.resolve()!=Path(f'/run/user/{os.getuid()}')
assert os.environ['AT_SPI_BUS_ADDRESS'].startswith('unix:path='+str(runtime)+'/')
pid=int(sys.argv[1]);output=Path(sys.argv[2]);Atspi.init()
rows=[]
def walk(node,depth=0):
    assert depth<16 and len(rows)<64
    role=node.get_role();states=node.get_state_set()
    row={'path':node.path,'role':int(role),'focused':states.contains(Atspi.StateType.FOCUSED),'editable':states.contains(Atspi.StateType.EDITABLE),'depth':depth}
    if role==Atspi.Role.PASSWORD_TEXT:rows.append(row);return
    text=node.get_text_iface()
    if text:
        row.update(text=Atspi.Text.get_text(text,0,-1),characters=Atspi.Text.get_character_count(text),caret=Atspi.Text.get_caret_offset(text),selections=[])
        for i in range(Atspi.Text.get_n_selections(text)):
            r=Atspi.Text.get_selection(text,i);row['selections'].append([r.start_offset,r.end_offset])
    rows.append(row)
    hyper=node.get_hypertext_iface()
    if hyper:
        row['links']=[]
        for i in range(hyper.get_n_links()):
            link=hyper.get_link(i);child=link.get_object(0)
            row['links'].append({'start':link.get_start_index(),'end':link.get_end_index(),'object':child.path if child else None})
            if child:walk(child,depth+1)
desktop=Atspi.get_desktop(0)
for i in range(desktop.get_child_count()):
    app=desktop.get_child_at_index(i)
    if app.get_process_id()!=pid:continue
    rule=Atspi.MatchRule.new(Atspi.StateSet.new([Atspi.StateType.FOCUSED]),Atspi.CollectionMatchType.ALL,None,Atspi.CollectionMatchType.ALL,None,Atspi.CollectionMatchType.ALL,None,Atspi.CollectionMatchType.ALL,False)
    for node in app.get_collection_iface().get_matches(rule,Atspi.CollectionSortOrder.CANONICAL,65,True):
        if node.get_state_set().contains(Atspi.StateType.EDITABLE):
            walk(node)
            current=node
            for _ in range(32):
                if current.get_role()==Atspi.Role.DOCUMENT_WEB:
                    doc=current.get_document_iface()
                    selections=doc.get_text_selections()
                    rows.append({'document_selections':[
                        {'start_object':r.start_object.path,'start_offset':r.start_offset,
                         'end_object':r.end_object.path,'end_offset':r.end_offset,
                         'start_is_active':bool(r.start_is_active)} for r in selections]})
                    break
                current=current.get_parent()
                if not current:break
output.write_text(json.dumps(rows,ensure_ascii=False,indent=2))
